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
 */
#ifndef ARTWORK_SERVICE_H
#define ARTWORK_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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

#define ARTWORK_JPEG_STAGING_BYTES (96u * 1024u)   /* MEMORY.md's budget -- measured against
                                                      * real fetched image sizes in Tier 6 */
#define ARTWORK_PLAYBACK_SUB_DEPTH 4

typedef struct {
    core_eventbus_t *bus;
    core_state_t       *state_store;
    time_source_t          clock;
    net_http_t                *http;
    http_policy_t                 http_policy;   /* i.scdn.co's own policy, distinct from
                                                     * Spotify's cloud-API policy */
} svc_artwork_cfg_t;

typedef struct {
    artwork_cache_t cache;
    uint8_t         *jpeg_staging;   /* heap_caps_malloc'd (PSRAM); NULL while suspended */

    eventbus_sub_h  playback_sub;
    event_t         playback_sub_storage[ARTWORK_PLAYBACK_SUB_DEPTH];

    char             last_track_id[PLAYBACK_TRACK_ID_MAX];
    artwork_handle_t current_handle;   /* generation == 0 means "none" -- core_pool_slot_t's
                                          * own contract: generation bumps on acquire, so a
                                          * genuinely acquired handle is never generation 0 */

    core_eventbus_t *bus;
    core_state_t       *state_store;
    time_source_t          clock;
    net_http_t                *http;
    http_policy_t                 http_policy;

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

/* Wired to the OTA-coexistence relay (app_wiring, Tier 4) -- PLATFORM.md
 * §2.4: actually frees the pool's backing memory, not just pauses
 * fetches. suspend() clears artwork_state_t.handle and publishes
 * ARTWORK_EVT_FAILED BEFORE freeing -- see artwork_cache.h's ownership
 * contract for why the ordering matters. resume() reallocates fresh. */
bool svc_artwork_suspend(svc_artwork_t *a);
bool svc_artwork_resume(svc_artwork_t *a);

#ifdef __cplusplus
}
#endif

#endif /* ARTWORK_SERVICE_H */
