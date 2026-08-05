/*
 * The fetch/decode/publish path, split out of artwork_service.c for the
 * 400-line gate -- that file was already at 398 when handle_track_change()
 * needed its suspended guard (see artwork_service.h's suspend contract for
 * the crash that guard fixes).
 *
 * artwork_service.c keeps service lifecycle: create/teardown/restart, the
 * task loop, and suspend/resume.
 */
#include "artwork_service_fetch.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "svc_artwork";

void artwork_service_publish_failed(svc_artwork_t *a)
{
    artwork_state_t st = { 0 };
    core_state_write(a->state_store, DOMAIN_ARTWORK, LINK_STATUS_OK, &st, sizeof(st));

    event_t evt = { .topic = TOPIC_ARTWORK };
    evt.u.artwork.kind = ARTWORK_EVT_FAILED;
    core_eventbus_publish(a->bus, &evt);
}

static void mark_track_done(svc_artwork_t *a, const char *track_id)
{
    strncpy(a->last_track_id, track_id, sizeof(a->last_track_id) - 1);
    a->last_track_id[sizeof(a->last_track_id) - 1] = '\0';
}

void artwork_service_handle_track_change(svc_artwork_t *a)
{
    /* Suspended: BOTH the cache backing memory and jpeg_staging are freed
     * (artwork_service.h's suspend contract, which records the real crash
     * this guard fixes). At the very top, not just around the fetch -- the
     * release/publish paths below touch a->cache, which is equally gone. */
    if (a->jpeg_staging == NULL) {
        return;
    }

    playback_state_t pb;
    if (!core_state_read(a->state_store, DOMAIN_PLAYBACK, &pb, sizeof(pb))) {
        return;
    }

    if (pb.track_id[0] == '\0') {
        /* Nothing playing anywhere. Tier -1: svc_spotify now publishes this
         * transition (NO_ACTIVE_DEVICE fix) -- previously a held handle
         * stuck on screen forever after playback stopped, since this
         * function never learned. Release+clear once, not every poll. */
        if (a->current_handle.generation != 0) {
            artwork_cache_release(&a->cache, a->current_handle);
            a->current_handle = (artwork_handle_t){ 0 };
        }
        if (a->last_track_id[0] != '\0') {
            a->last_track_id[0] = '\0';
            artwork_service_publish_failed(a);
        }
        return;
    }
    if (pb.artwork_url_640[0] == '\0') {
        /* Playing, but this track has no artwork URL. A bare return here
         * (found by review) left the PREVIOUS track's cover on screen under
         * this track's own metadata. Same release+clear+publish-once
         * treatment as "nothing playing" above, keyed off last_track_id so
         * repeat idle polls don't republish every tick. */
        if (strcmp(pb.track_id, a->last_track_id) != 0) {
            if (a->current_handle.generation != 0) {
                artwork_cache_release(&a->cache, a->current_handle);
                a->current_handle = (artwork_handle_t){ 0 };
            }
            strncpy(a->last_track_id, pb.track_id, sizeof(a->last_track_id) - 1);
            artwork_service_publish_failed(a);
        }
        return;
    }
    if (strcmp(pb.track_id, a->last_track_id) == 0) {
        return;   /* already succeeded, or already gave up after
                      ARTWORK_FETCH_MAX_ATTEMPTS -- only re-attempt on a
                      real track-id change */
    }

    char fetching_track_id[PLAYBACK_TRACK_ID_MAX];
    strncpy(fetching_track_id, pb.track_id, sizeof(fetching_track_id) - 1);
    fetching_track_id[sizeof(fetching_track_id) - 1] = '\0';

    /* Real bug, found live: marking a track "done" only on success left
     * last_track_id unchanged on failure, so a cover that repeatedly failed
     * to decode (confirmed: JDR_INP on the same real cover across many
     * independent fetches) got re-fetched every poll for as long as it
     * kept playing, hammering i.scdn.co with an identical, doomed request.
     * attempt_track_id/attempt_count (tracked separately from
     * last_track_id, so an in-progress retry isn't mistaken for "done"
     * above) give a real, bounded chance to recover from transient network
     * flakiness on this long-lived streamed read, without retrying a
     * permanently-bad cover forever.
     *
     * On what JDR_INP actually means, because this comment previously
     * implied the decoder couldn't handle that cover's FORMAT and sent a
     * later reader chasing the wrong thing: it does not. TJpgDec reports an
     * unsupported JPEG standard (progressive, arithmetic-coded, CMYK, exotic
     * chroma subsampling) as JDR_FMT3, and a bad quantization table as
     * JDR_FMT1. Every single `return JDR_INP` in tjpgd.c is guarded by the
     * input callback returning FEWER BYTES THAN ASKED FOR -- it means the
     * decoder ran off the end of the buffer. Two candidates, both real and
     * both deterministic per-image, which is why it reproduced every time:
     *   1. The fetch was truncated. net_http_request() breaks out of its read
     *      loop with `buffer full -- truncate, not an error` and returns
     *      TRUE, so a short body reaches the decoder looking like a success.
     *      The resp.len line logged below is what tells these apart.
     *   2. The decoder desynced on a non-conforming marker and walked past
     *      the end of a COMPLETE buffer. Fixed upstream in esp_jpeg 1.2.1,
     *      but that fix lives only in the vendored TJpgDec -- which this
     *      project did not build until CONFIG_JD_USE_ROM=n (sdkconfig.defaults).
     * Switching to espressif/esp_new_jpeg was evaluated and rejected for
     * this: it is baseline-only too, so it cannot decode anything TJpgDec
     * rejects on format grounds, and it would cost internal RAM. */
    if (strcmp(fetching_track_id, a->attempt_track_id) != 0) {
        strncpy(a->attempt_track_id, fetching_track_id, sizeof(a->attempt_track_id) - 1);
        a->attempt_track_id[sizeof(a->attempt_track_id) - 1] = '\0';
        a->attempt_count = 0;
    }
    a->attempt_count++;
    bool last_attempt = (a->attempt_count >= ARTWORK_FETCH_MAX_ATTEMPTS);

    net_http_request_opts_t opts = { .method = NET_HTTP_METHOD_GET, .retryable_after_send = true };
    net_http_response_t resp = { 0 };
    bool ok = net_http_request(a->http, pb.artwork_url_640, &opts, &a->http_policy,
                                  (char *)a->jpeg_staging, ARTWORK_JPEG_STAGING_BYTES, &resp);
    if (!ok || resp.status_code < 200 || resp.status_code >= 300) {
        ESP_LOGW(TAG, "artwork fetch failed: status=%d attempt=%u/%u", resp.status_code,
                   (unsigned)a->attempt_count, (unsigned)ARTWORK_FETCH_MAX_ATTEMPTS);
        if (last_attempt) {
            mark_track_done(a, fetching_track_id);
        }
        artwork_service_publish_failed(a);
        return;
    }

    /* Stale-in-flight-fetch discard: re-check before publishing, discard
     * silently if the track changed again during the fetch/decode above --
     * the newer track's own fetch is already queued for a later tick. */
    playback_state_t pb_now;
    if (core_state_read(a->state_store, DOMAIN_PLAYBACK, &pb_now, sizeof(pb_now)) &&
        strcmp(pb_now.track_id, fetching_track_id) != 0) {
        return;
    }

    artwork_handle_t new_handle;
    if (!artwork_cache_acquire(&a->cache, &new_handle)) {
        ESP_LOGW(TAG, "artwork pool exhausted");
        if (last_attempt) {
            mark_track_done(a, fetching_track_id);
        }
        artwork_service_publish_failed(a);
        return;
    }
    uint16_t *pixels = artwork_cache_lock(&a->cache, new_handle);
    if (pixels == NULL || !artwork_decode_jpeg_to_rgb565(a->jpeg_staging, resp.len, pixels)) {
        /* resp.len logged against the cap: == means genuinely truncated
         * (raise the cap); < means the connection closed early (a network
         * issue the retry above covers, not the buffer size). */
        ESP_LOGW(TAG, "artwork decode failed: resp.len=%u staging_cap=%u attempt=%u/%u",
                   (unsigned)resp.len, (unsigned)ARTWORK_JPEG_STAGING_BYTES,
                   (unsigned)a->attempt_count, (unsigned)ARTWORK_FETCH_MAX_ATTEMPTS);
        artwork_cache_release(&a->cache, new_handle);
        if (last_attempt) {
            mark_track_done(a, fetching_track_id);
        }
        artwork_service_publish_failed(a);
        return;
    }

    artwork_pixel_buffer_t buf = { pixels, ARTWORK_CACHE_SLOT_WIDTH, ARTWORK_CACHE_SLOT_HEIGHT };
    uint32_t color = artwork_palette_dominant_color_rgb565(&buf);

    artwork_state_t st = { .handle = new_handle, .dominant_color_rgb565 = color };
    strncpy(st.track_id, fetching_track_id, sizeof(st.track_id) - 1);
    core_state_write(a->state_store, DOMAIN_ARTWORK, LINK_STATUS_OK, &st, sizeof(st));

    event_t evt = { .topic = TOPIC_ARTWORK };
    evt.u.artwork.kind                  = ARTWORK_EVT_READY;
    evt.u.artwork.handle               = new_handle;
    evt.u.artwork.dominant_color_rgb565 = color;
    core_eventbus_publish(a->bus, &evt);

    /* Release the PREVIOUS handle strictly AFTER publishing the new one --
     * artwork_cache.h's own ownership contract: there is never a window
     * where core_state/an in-flight event points at an already-released
     * slot. generation == 0 means "no previous handle," core_pool_slot_t's
     * own contract (bumps on acquire, so a real handle is never gen 0). */
    if (a->current_handle.generation != 0) {
        artwork_cache_release(&a->cache, a->current_handle);
    }
    a->current_handle = new_handle;
    mark_track_done(a, fetching_track_id);
}
