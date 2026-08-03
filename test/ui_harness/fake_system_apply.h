/*
 * Harness-only substitute for view_model_system_apply.c -- that file calls
 * 3 real ESP-IDF APIs directly (esp_wifi_sta_get_ap_info(), esp_ota_get_
 * app_description(), esp_read_mac()), none of which exist on host, so it
 * can't be linked into this SDL harness at all. This mirrors its exact
 * struct/init/tick shape but with fixed fake values standing in for those
 * 3 calls -- same "fake_* substitute for a target-only real thing" idiom
 * fake_artwork_pixel_provider.c/fake_intent_submit_provider.c already use
 * here. Reuses the real, portable view_model_system_build() (pure, no IDF
 * dependency) unchanged.
 */
#ifndef FAKE_SYSTEM_APPLY_H
#define FAKE_SYSTEM_APPLY_H

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
} fake_system_apply_t;

bool fake_system_apply_init(fake_system_apply_t *a, core_state_t *state, core_eventbus_t *bus,
                                ui_router_t *router, time_source_t clock);
void fake_system_apply_tick(fake_system_apply_t *a);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_SYSTEM_APPLY_H */
