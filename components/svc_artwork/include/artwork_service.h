/*
 * The artwork service: owns the "artwork" task (ARCHITECTURE.md §4: core 0,
 * prio 3, 6KB, on-track-change cadence, blocking allowed/long -- JPEG
 * decode is a real CPU burn, avg 660ms/max ~1.8s per RISKS.md R1).
 *
 * Genuinely new ground vs. svc_denon's settings-relay: this subscribes to
 * TOPIC_PLAYBACK directly, no app_wiring mediation needed -- "the current
 * track changed" is satisfiable through core-layer primitives alone
 * (a bus subscription + core_state_read()), both legal svc->core
 * operations regardless of which other svc publishes/writes there.
 *
 * Target-only, same reason as svc_denon/svc_spotify (embeds a net_http_t
 * pointer). Deliberately NOT split into a pure state file the way
 * denon_service_state.h/spotify_service_state.h are: there is no FSM here
 * to protect restart-correctness for, just a pool + a dedupe string --
 * test_core_pool.c already covers core_pool_slot_t's own generic
 * acquire/release/reset correctness generically; the real allocation path
 * (heap_caps_malloc, real JPEG decode) is verified by idf.py build +
 * Tier 6 hardware, the same way spotify_auth.c's mbedtls/esp_random code
 * is.
 *
 * Handle ownership contract: see artwork_cache.h's own top comment.
 * on_release for TOPIC_ARTWORK is NULL (app_wiring, Tier 4) -- this
 * service alone releases handles, on its own schedule.
 *
 * Phase 5 Tier -1: suspend()/resume() are task-owned (see
 * svc_artwork_request_suspend/resume below) -- a review pass found that
 * calling them directly from a different task's context (the OTA relay's)
 * could free a->jpeg_staging while this task's own handle_track_change()
 * was still fetching into or decoding from it, a race no cache-level mutex
 * can close (that buffer sits entirely outside the cache pool). Routing
 * suspend/resume through a request the task drains itself means they can
 * never preempt that same task's own in-flight fetch/decode.
 */
#ifndef ARTWORK_SERVICE_H
#define ARTWORK_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "artwork_pixel_types.h"   /* artwork_pixel_provider_t */
#include "core_eventbus.h"
#include "core_state.h"
#include "lifecycle_types.h"
#include "net_http.h"
#include "os_if_types.h"

#include "artwork_cache.h"
#include "artwork_decode.h"
#include "artwork_palette.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Real bug, found live (Tier 7): 96 KiB (MEMORY.md's original budget, from
 * Tier 6's own measurement) truncated a real fetched image -- confirmed by
 * the exact failure signature (esp_jpeg_decode returning JDR_INP, "wrong
 * termination of input stream" -- tjpgd.h's own error code 2, not a
 * corrupt-data or format error). net_http_request()'s own contract is
 * silent truncation, not an error, same mechanism that undersized
 * spotify_api_get_player_state()'s buffer earlier this same tier. Tier 6's
 * own measurement was real but evidently not against a wide enough sample
 * of real cover art. Raised with real margin, not just enough to clear
 * one failure -- PSRAM cost is negligible against ~3 MiB free.
 *
 * Raised again, 256 KiB -> 512 KiB (real hardware, post-Tier-7): the SAME
 * JDR_INP signature recurred on a specific real cover (Capital Cities,
 * "Safe and Sound") even at 256 KiB, confirmed reproducible across many
 * independent fetches of the identical URL -- not explained away as
 * transient network flakiness, since a transient failure wouldn't repeat
 * identically every single time. artwork_service.c's own decode-failure
 * log now also prints resp.len against this constant, so a future
 * recurrence is self-diagnosing (resp.len == this value means genuinely
 * truncated by this cap; resp.len < this value means the server/connection
 * itself closed early, a real network issue this buffer size can't fix --
 * see handle_track_change()'s own bounded-retry comment for that case). */
#define ARTWORK_JPEG_STAGING_BYTES (512u * 1024u)
#define ARTWORK_PLAYBACK_SUB_DEPTH 4
#define ARTWORK_FETCH_MAX_ATTEMPTS 3   /* handle_track_change()'s own comment */

typedef struct {
    core_eventbus_t *bus;
    core_state_t       *state_store;
    time_source_t          clock;
    net_http_t                *http;
    http_policy_t                 http_policy;   /* i.scdn.co's own policy, distinct from
                                                     * Spotify's cloud-API policy */
} svc_artwork_cfg_t;

typedef enum {
    ARTWORK_LIFECYCLE_REQUEST_NONE = 0,
    ARTWORK_LIFECYCLE_REQUEST_SUSPEND,
    ARTWORK_LIFECYCLE_REQUEST_RESUME,
} artwork_lifecycle_request_t;

typedef struct {
    artwork_cache_t cache;
    uint8_t         *jpeg_staging;   /* heap_caps_malloc'd (PSRAM); NULL while suspended --
                                        * only ever touched by the artwork task itself */

    eventbus_sub_h  playback_sub;
    event_t         playback_sub_storage[ARTWORK_PLAYBACK_SUB_DEPTH];

    char             last_track_id[PLAYBACK_TRACK_ID_MAX];   /* set once this track is
                                                                 DONE -- either a successful
                                                                 decode, or ARTWORK_FETCH_
                                                                 MAX_ATTEMPTS exhausted --
                                                                 handle_track_change() skips
                                                                 entirely once this matches */
    char                attempt_track_id[PLAYBACK_TRACK_ID_MAX];   /* which track attempt_count
                                                                     * below is counting for --
                                                                     * separate from last_track_id
                                                                     * so a still-in-progress
                                                                     * retry sequence (not yet
                                                                     * given up on) doesn't get
                                                                     * skipped by the check above */
    uint8_t                attempt_count;
    artwork_handle_t current_handle;   /* generation == 0 means "none" -- core_pool_slot_t's
                                          * own contract: generation bumps on acquire, so a
                                          * genuinely acquired handle is never generation 0 */

    core_eventbus_t *bus;
    core_state_t       *state_store;
    time_source_t          clock;
    net_http_t                *http;
    http_policy_t                 http_policy;

    SemaphoreHandle_t             lifecycle_lock;     /* guards lifecycle_request only --
                                                          NOT cache access (that's cache.lock) */
    artwork_lifecycle_request_t   lifecycle_request;   /* set by svc_artwork_request_suspend/
                                                          resume (any task), drained once per
                                                          artwork-task loop iteration */

    TaskHandle_t         task_handle;
    volatile bool           task_should_stop;
    SemaphoreHandle_t          stopped_sem;
} svc_artwork_t;

bool svc_artwork_create(svc_artwork_t *a, const svc_artwork_cfg_t *cfg);
void svc_artwork_teardown(void *handle);

typedef struct {
    svc_artwork_t     *a;
    svc_artwork_cfg_t   cfg;
} svc_artwork_restart_ctx_t;

bool svc_artwork_restart(void *restart_ctx, void **out_handle);
service_desc_t svc_artwork_service_desc(svc_artwork_restart_ctx_t *restart_ctx);

/* Requests the artwork task suspend/resume itself at its own next loop
 * iteration -- PLATFORM.md §2.4: suspend actually frees the pool's backing
 * memory (and jpeg_staging), not just pauses fetches; resume reallocates
 * fresh. Safe to call from any task (this is the ONLY safe way to trigger
 * suspend/resume from outside the artwork task -- see this file's top
 * comment). Wired to the OTA-coexistence relay (app_wiring, Tier 4). A
 * request overwrites a not-yet-drained prior request of a different kind;
 * callers should avoid issuing a redundant same-kind request
 * (app_wiring_ticks.c's artwork_suspended_for_ota guard; main/pair_http.c's
 * own s_server != NULL early return).
 *
 * WHILE SUSPENDED, EVERYTHING THIS SERVICE OWNS IS GONE -- not paused. Both
 * the cache backing memory and jpeg_staging are freed, current_handle and
 * last_track_id are cleared. Any code path that touches either must check
 * `a->jpeg_staging == NULL` first; that is the canonical "am I suspended"
 * test, and handle_track_change() opens with it.
 *
 * That guard exists because of a real StoreProhibited crash on real hardware
 * (EXCVADDR 0 -- a memcpy into a NULL destination inside
 * esp_http_client_read, reached via net_http_request from
 * handle_track_change). jpeg_staging IS the destination buffer that function
 * hands net_http_request(), and nothing checked it. The task loop's drain
 * ordering was believed to cover this and did not: draining first only makes
 * the suspend happen first, within the same iteration, after which the fetch
 * ran anyway.
 *
 * It stayed latent for as long as the OTA relay was the only caller -- an
 * OTA reboots within seconds and nobody changes tracks during one. The
 * Spotify pair overlay (adr/0011) suspends artwork for as long as someone
 * leaves that screen open, during live playback, so the next playback event
 * fetched into freed memory. suspend_now() clearing last_track_id removes
 * the early-return that might otherwise have masked it.
 *
 * NOTE for future callers reaching for this to free INTERNAL RAM: it does
 * not. Both buffers are MALLOC_CAP_SPIRAM. What it frees on the internal
 * heap is the transient 16.7 KB contiguous TLS record buffer a fetch would
 * otherwise allocate -- which is the real reason both current callers use
 * it, since both start something that wants internal RAM of its own. */
void svc_artwork_request_suspend(svc_artwork_t *a);
void svc_artwork_request_resume(svc_artwork_t *a);

/* Fills an artwork_pixel_provider_t backed by this service's cache -- hand
 * this to ui_app's construction config (app_wiring, Tier 4) so `ui` never
 * needs to include any svc_artwork header (Phase 5 plan §1). The returned
 * struct's ctx points at `a` itself; `a` must outlive every use of it,
 * which holds for the service's whole lifetime including across a
 * suspend/resume cycle (lock_copy() just returns false while suspended). */
artwork_pixel_provider_t svc_artwork_pixel_provider(svc_artwork_t *a);

#ifdef __cplusplus
}
#endif

#endif /* ARTWORK_SERVICE_H */
