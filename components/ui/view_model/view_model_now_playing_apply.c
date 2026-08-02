#include "view_model_now_playing_apply.h"

#include <string.h>

#include "view_model_text.h"
#include "w_toast.h"

/* EVENTS_AND_STATE.md §6.3 step 5: "A pending intent that goes unconfirmed
 * for 3 s is abandoned and reconciled." */
#define PENDING_TIMEOUT_US (3 * 1000 * 1000)

static bool wrapped_submit(void *ctx, intent_t *intent)
{
    view_model_now_playing_apply_t *a = (view_model_now_playing_apply_t *)ctx;
    if (a->real_submit.submit == NULL) {
        return false;
    }
    bool ok = a->real_submit.submit(a->real_submit.ctx, intent);
    if (ok) {
        a->pending          = true;
        a->pending_seq       = intent->seq;
        a->pending_since_us = a->clock.now_us(a->clock.ctx);
    }
    return ok;
}

intent_submit_provider_t view_model_now_playing_apply_intent_submit_provider(view_model_now_playing_apply_t *a)
{
    intent_submit_provider_t p = { .ctx = a, .submit = wrapped_submit };
    return p;
}

bool view_model_now_playing_apply_init(view_model_now_playing_apply_t *a, core_state_t *state,
                                          core_eventbus_t *bus, ui_router_t *router,
                                          screen_now_playing_ctx_t *screen_ctx,
                                          intent_submit_provider_t real_submit, time_source_t clock)
{
    a->state         = state;
    a->bus             = bus;
    a->router           = router;
    a->screen_ctx         = screen_ctx;
    a->real_submit          = real_submit;
    a->clock                  = clock;
    a->pending                  = false;
    a->pending_seq                    = 0;
    a->pending_since_us                = 0;
    a->glyph_fallback_count           = 0;

    if (!core_eventbus_subscribe(bus, TOPIC_PLAYBACK, a->playback_sub_storage, 4, &a->playback_sub)) {
        return false;
    }
    if (!core_eventbus_subscribe(bus, TOPIC_ARTWORK, a->artwork_sub_storage, 2, &a->artwork_sub)) {
        return false;
    }
    if (!core_eventbus_subscribe(bus, TOPIC_DENON, a->denon_sub_storage, 8, &a->denon_sub)) {
        return false;
    }
    if (!core_eventbus_subscribe(bus, TOPIC_INTENT_ACK, a->intent_ack_sub_storage, 4, &a->intent_ack_sub)) {
        return false;
    }
    if (!core_eventbus_subscribe(bus, TOPIC_TOAST, a->toast_sub_storage, 4, &a->toast_sub)) {
        return false;
    }
    return true;
}

void view_model_now_playing_apply_tick(view_model_now_playing_apply_t *a)
{
    event_t evt;

    /* PLAYBACK/ARTWORK/DENON events are "something changed, go re-read
     * core_state" signals only (event_types.h's own playback_evt_t
     * comment) -- draining them just keeps the ring from ever filling;
     * the actual re-read happens unconditionally below regardless. */
    while (core_eventbus_poll(a->bus, a->playback_sub, &evt)) { }
    while (core_eventbus_poll(a->bus, a->artwork_sub, &evt))  { }
    while (core_eventbus_poll(a->bus, a->denon_sub, &evt))    { }

    while (core_eventbus_poll(a->bus, a->intent_ack_sub, &evt)) {
        if (evt.topic != TOPIC_INTENT_ACK || !a->pending || evt.u.intent_ack.seq != a->pending_seq) {
            continue;   /* not the intent this glue is watching for (or nothing pending) */
        }
        if (evt.u.intent_ack.kind == INTENT_ACK_EVT_REJECTED) {
            w_toast_show(&a->screen_ctx->toast, "Couldn't reach Spotify -- try again");
        }
        a->pending = false;
    }

    while (core_eventbus_poll(a->bus, a->toast_sub, &evt)) {
        if (evt.topic == TOPIC_TOAST) {
            w_toast_show(&a->screen_ctx->toast, evt.u.toast.message);
        }
    }

    if (a->pending) {
        int64_t now_us = a->clock.now_us(a->clock.ctx);
        if (now_us - a->pending_since_us >= PENDING_TIMEOUT_US) {
            a->pending = false;   /* abandoned, not rejected -- no toast (§6.3 step 5) */
        }
    }

    playback_state_t pb    = { 0 };
    artwork_state_t  art   = { 0 };
    denon_state_t     denon = { 0 };
    core_state_read(a->state, DOMAIN_PLAYBACK, &pb, sizeof(pb));
    core_state_read(a->state, DOMAIN_ARTWORK, &art, sizeof(art));
    core_state_read(a->state, DOMAIN_DENON, &denon, sizeof(denon));

    screen_now_playing_update_t u;
    u.vm             = view_model_now_playing_build(&pb, &art, &denon, a->clock, a->pending);
    u.artwork_handle = art.handle;
    strncpy(u.artwork_track_id, art.track_id, sizeof(u.artwork_track_id) - 1);
    u.artwork_track_id[sizeof(u.artwork_track_id) - 1] = '\0';
    strncpy(u.playback_track_id, pb.track_id, sizeof(u.playback_track_id) - 1);
    u.playback_track_id[sizeof(u.playback_track_id) - 1] = '\0';

    /* RISKS.md R17's glyph-fallback counter -- title/artist are the only
     * two fields screen_now_playing.c actually renders (album only ever
     * reaches the screen as build_title_with_fallback()'s fallback INTO
     * title when title itself is empty, view_model_now_playing.c, so
     * scanning title+artist covers everything a real substitution could
     * be visible in). Cumulative, never reset -- same "counter, not rate"
     * contract as ui_diag_state_t's other two fields (app_state_types.h). */
    a->glyph_fallback_count += (uint32_t)view_model_text_count_replacements(u.vm.title);
    a->glyph_fallback_count += (uint32_t)view_model_text_count_replacements(u.vm.artist);

    ui_router_update(a->router, UI_SCREEN_NOW_PLAYING, &u);
}

uint32_t view_model_now_playing_apply_glyph_fallback_count(const view_model_now_playing_apply_t *a)
{
    return a->glyph_fallback_count;
}
