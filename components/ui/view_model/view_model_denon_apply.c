#include "view_model_denon_apply.h"

#include "view_model_denon.h"

bool view_model_denon_apply_init(view_model_denon_apply_t *a, core_state_t *state,
                                     core_eventbus_t *bus, ui_router_t *router)
{
    a->state  = state;
    a->bus    = bus;
    a->router = router;

    if (!core_eventbus_subscribe(bus, TOPIC_DENON, a->denon_sub_storage, 8, &a->denon_sub)) {
        return false;
    }
    if (!core_eventbus_subscribe(bus, TOPIC_PLAYBACK, a->playback_sub_storage, 4, &a->playback_sub)) {
        return false;
    }
    return true;
}

void view_model_denon_apply_tick(view_model_denon_apply_t *a)
{
    event_t evt;

    /* Signals only -- "something changed, go re-read core_state" (same
     * contract view_model_now_playing_apply_tick()'s own DENON/PLAYBACK
     * drains use). The actual re-read happens unconditionally below. */
    while (core_eventbus_poll(a->bus, a->denon_sub, &evt))    { }
    while (core_eventbus_poll(a->bus, a->playback_sub, &evt)) { }

    denon_state_t    denon = { 0 };
    playback_state_t pb    = { 0 };
    core_state_read(a->state, DOMAIN_DENON, &denon, sizeof(denon));
    core_state_read(a->state, DOMAIN_PLAYBACK, &pb, sizeof(pb));

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    ui_router_update(a->router, UI_SCREEN_DENON, &vm);
}
