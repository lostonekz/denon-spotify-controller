/*
 * Target-only glue driving the System screen. Subscribes TOPIC_NET/
 * TOPIC_DENON/TOPIC_SPOTIFY (signals only), and each tick also reaches
 * directly into 3 plain ESP-IDF APIs the pure builder itself never touches
 * (esp_wifi_sta_get_ap_info() for RSSI, esp_ota_get_app_description() for
 * firmware version/date, a MAC-based panel identifier) -- `ui` already has
 * a legal, precedented direct `idf` edge (ui_app.c's own esp_timer_get_
 * time() call is the same category), so none of this needs a core_state
 * domain or an app_wiring relay.
 */
#ifndef VIEW_MODEL_SYSTEM_APPLY_H
#define VIEW_MODEL_SYSTEM_APPLY_H

#include <stdbool.h>

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
} view_model_system_apply_t;

bool view_model_system_apply_init(view_model_system_apply_t *a, core_state_t *state,
                                      core_eventbus_t *bus, ui_router_t *router, time_source_t clock);
void view_model_system_apply_tick(view_model_system_apply_t *a);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_SYSTEM_APPLY_H */
