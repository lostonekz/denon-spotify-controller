#include "fake_system_apply.h"

#include "view_model_system.h"

bool fake_system_apply_init(fake_system_apply_t *a, core_state_t *state, core_eventbus_t *bus,
                                ui_router_t *router, time_source_t clock)
{
    a->state  = state;
    a->bus    = bus;
    a->router = router;
    a->clock  = clock;

    if (!core_eventbus_subscribe(bus, TOPIC_NET, a->net_sub_storage, 4, &a->net_sub)) {
        return false;
    }
    if (!core_eventbus_subscribe(bus, TOPIC_DENON, a->denon_sub_storage, 8, &a->denon_sub)) {
        return false;
    }
    if (!core_eventbus_subscribe(bus, TOPIC_SPOTIFY, a->spotify_sub_storage, 4, &a->spotify_sub)) {
        return false;
    }
    return true;
}

void fake_system_apply_tick(fake_system_apply_t *a)
{
    event_t evt;
    while (core_eventbus_poll(a->bus, a->net_sub, &evt))     { }
    while (core_eventbus_poll(a->bus, a->denon_sub, &evt))    { }
    while (core_eventbus_poll(a->bus, a->spotify_sub, &evt)) { }

    net_state_t     net     = { 0 };
    denon_state_t    denon   = { 0 };
    spotify_state_t spotify = { 0 };
    core_state_read(a->state, DOMAIN_NET, &net, sizeof(net));
    core_state_read(a->state, DOMAIN_DENON, &denon, sizeof(denon));
    core_state_read(a->state, DOMAIN_SPOTIFY, &spotify, sizeof(spotify));

    /* Fixed fake values standing in for the 3 real IDF calls
     * view_model_system_apply.c makes on target -- see this file's own
     * header comment. */
    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, -48, "harness-fake",
                                                 "harness build", "HARNESS", a->clock);
    ui_router_update(a->router, UI_SCREEN_SYSTEM, &vm);
}
