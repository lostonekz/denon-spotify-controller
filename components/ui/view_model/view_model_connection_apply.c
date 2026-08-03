#include "view_model_connection_apply.h"

#include "view_model_connection.h"

bool view_model_connection_apply_init(view_model_connection_apply_t *a, core_state_t *state,
                                          core_eventbus_t *bus, ui_router_t *router)
{
    a->state  = state;
    a->bus    = bus;
    a->router = router;

    return core_eventbus_subscribe(bus, TOPIC_DENON, a->denon_sub_storage, 8, &a->denon_sub);
}

void view_model_connection_apply_tick(view_model_connection_apply_t *a)
{
    event_t evt;
    while (core_eventbus_poll(a->bus, a->denon_sub, &evt)) { }

    denon_state_t denon = { 0 };
    core_state_read(a->state, DOMAIN_DENON, &denon, sizeof(denon));

    connection_vm_t vm = view_model_connection_build(&denon);
    ui_router_update(a->router, UI_SCREEN_CONNECTION, &vm);
}
