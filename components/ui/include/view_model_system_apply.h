/*
 * Target-only glue driving the System screen. Subscribes TOPIC_NET/
 * TOPIC_DENON/TOPIC_SPOTIFY (signals only), and also reaches directly into
 * 3 plain ESP-IDF APIs the pure builder itself never touches
 * (esp_wifi_sta_get_ap_info() for RSSI, esp_ota_get_app_description() for
 * firmware version/date, a MAC-based panel identifier) -- `ui` already has
 * a legal, precedented direct `idf` edge (ui_app.c's own esp_timer_get_
 * time() call is the same category), so none of this needs a core_state
 * domain or an app_wiring relay.
 *
 * Those 3 calls are deliberately NOT made per tick. This glue's tick runs
 * on every ui_app_task iteration (~60x/second, alongside the other three
 * screens' apply ticks), and all 3 were originally issued on every one of
 * them, regardless of which screen was actually on display -- they run
 * before the ui_router_update() call that would otherwise no-op for an
 * inactive screen. Two of the three return values that never change for
 * the life of the boot (the app descriptor, and a panel id derived from
 * the station MAC), so they are read once in _init() and cached in the
 * fields below. The third, RSSI, does change -- but it is a blocking call
 * into the WiFi driver (a lock plus coordination with the WiFi task), and
 * sampling it at render rate to feed a once-a-second display was the most
 * expensive thing this file did, on the task least able to afford it
 * (RISKS.md R1's CPU/PSRAM contention with the LCD scanout). It is now
 * sampled on its own interval and the last value reused in between.
 */
#ifndef VIEW_MODEL_SYSTEM_APPLY_H
#define VIEW_MODEL_SYSTEM_APPLY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_app_desc.h"   /* esp_app_desc_t -- cached by pointer, see below */

#include "core_eventbus.h"
#include "core_state.h"
#include "os_if_types.h"
#include "ui_router.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    core_state_t   *state;
    core_eventbus_t   *bus;
    ui_router_t         *router;
    time_source_t          clock;

    eventbus_sub_h    net_sub;
    event_t                net_sub_storage[4];
    eventbus_sub_h    denon_sub;
    event_t                denon_sub_storage[8];
    eventbus_sub_h    spotify_sub;
    event_t                spotify_sub_storage[4];

    /* Read once in _init(), then reused -- both are fixed for the life of
     * the boot. app_desc points into the running app's own descriptor in
     * memory-mapped flash (esp_ota_get_app_description() returns a pointer
     * to it, not a copy), so holding it is a pointer copy, not a buffer. */
    const esp_app_desc_t *app_desc;
    char                      panel_id[16];   /* "AABBCC" + NUL, from the station MAC */

    /* RSSI, resampled on its own interval rather than per tick. */
    int8_t                     rssi_dbm;
    int64_t                     rssi_sampled_us;
    bool                          rssi_ever_sampled;
} view_model_system_apply_t;

bool view_model_system_apply_init(view_model_system_apply_t *a, core_state_t *state,
                                      core_eventbus_t *bus, ui_router_t *router, time_source_t clock);
void view_model_system_apply_tick(view_model_system_apply_t *a);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_SYSTEM_APPLY_H */
