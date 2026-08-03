/*
 * Target-only glue driving the Connection screen -- mirrors view_model_
 * denon_apply.h's exact shape (subscribes TOPIC_DENON only, no pending/ACK
 * state, unconditional rebuild every tick).
 */
#ifndef VIEW_MODEL_CONNECTION_APPLY_H
#define VIEW_MODEL_CONNECTION_APPLY_H

#include <stdbool.h>

#include "core_eventbus.h"
#include "core_state.h"
#include "ui_router.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    core_state_t   *state;
    core_eventbus_t   *bus;
    ui_router_t         *router;

    eventbus_sub_h    denon_sub;
    event_t                denon_sub_storage[8];
} view_model_connection_apply_t;

bool view_model_connection_apply_init(view_model_connection_apply_t *a, core_state_t *state,
                                          core_eventbus_t *bus, ui_router_t *router);
void view_model_connection_apply_tick(view_model_connection_apply_t *a);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_CONNECTION_APPLY_H */
