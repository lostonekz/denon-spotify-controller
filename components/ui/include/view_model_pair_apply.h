/*
 * Glue driving the Spotify pair overlay. Same shape as
 * view_model_connection_apply.h (subscribe, drain, rebuild every tick), with
 * two pieces of real state the pure builder deliberately does not hold:
 *
 *  - `was_pairing`, the latch that tells "UNPAIRED because we never paired"
 *    apart from "UNPAIRED because the exchange just failed" -- the same
 *    session_status, very different things to tell someone.
 *  - `listener_running`, so the QR is never shown advertising a port with
 *    nothing behind it.
 *
 * Unlike the other apply-glue in this project, this one is portable enough to
 * run in test/ui_harness -- it makes no direct ESP-IDF calls.
 */
#ifndef VIEW_MODEL_PAIR_APPLY_H
#define VIEW_MODEL_PAIR_APPLY_H

#include <stdbool.h>
#include <stdint.h>

#include "core_eventbus.h"
#include "core_state.h"
#include "ui_router.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    core_state_t    *state;
    core_eventbus_t *bus;
    ui_router_t     *router;
    uint16_t          port;

    bool              was_pairing;
    bool              listener_running;

    eventbus_sub_h    net_sub;
    event_t             net_sub_storage[4];
    eventbus_sub_h    spotify_sub;
    event_t             spotify_sub_storage[4];
} view_model_pair_apply_t;

bool view_model_pair_apply_init(view_model_pair_apply_t *a, core_state_t *state,
                                    core_eventbus_t *bus, ui_router_t *router, uint16_t port);

/* Call unconditionally from the UI task loop -- it drains its subscriptions
 * every tick regardless (so no event ring fills while the overlay is closed)
 * and only does real work when the overlay is actually presented. */
void view_model_pair_apply_tick(view_model_pair_apply_t *a);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_PAIR_APPLY_H */
