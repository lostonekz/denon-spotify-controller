#include "view_model_pair_apply.h"

#include "view_model_pair.h"

bool view_model_pair_apply_init(view_model_pair_apply_t *a, core_state_t *state,
                                    core_eventbus_t *bus, ui_router_t *router, uint16_t port)
{
    a->state            = state;
    a->bus              = bus;
    a->router           = router;
    a->port             = port;
    a->was_pairing      = false;
    a->listener_running = false;

    if (!core_eventbus_subscribe(bus, TOPIC_NET, a->net_sub_storage, 4, &a->net_sub)) {
        return false;
    }
    return core_eventbus_subscribe(bus, TOPIC_SPOTIFY, a->spotify_sub_storage, 4, &a->spotify_sub);
}

void view_model_pair_apply_tick(view_model_pair_apply_t *a)
{
    /* Drained unconditionally, before the presented check -- a subscription
     * that stops being read does not stop being written. */
    event_t evt;
    while (core_eventbus_poll(a->bus, a->net_sub, &evt)) { }
    while (core_eventbus_poll(a->bus, a->spotify_sub, &evt)) { }

    if (!ui_router_overlay_is_presented(a->router)) {
        /* Closed. Reset both latches so the next open starts clean rather
         * than showing the previous session's outcome -- in particular, a
         * stale was_pairing would render a healthy device's "already
         * connected, confirm to re-pair" state as a triumphant "Spotify
         * connected" the instant the overlay reopened. */
        a->was_pairing      = false;
        a->listener_running = false;
        return;
    }

    /* mount() submitted PAIRING_INTENT_START before this ever runs, so by the
     * time the overlay is presented the listener has been asked for. This is
     * optimistic by one intent-dispatch hop; main/pair_http.c starts the
     * server BEFORE beginning a pairing exchange, so the state that actually
     * gates the QR below (session_status == PAIRING) cannot be reached unless
     * the listener really did come up. */
    a->listener_running = true;

    net_state_t     net     = { 0 };
    spotify_state_t spotify = { 0 };
    core_state_read(a->state, DOMAIN_NET, &net, sizeof(net));
    core_state_read(a->state, DOMAIN_SPOTIFY, &spotify, sizeof(spotify));

    if (spotify.session_status == SPOTIFY_SESSION_PAIRING) {
        a->was_pairing = true;
    }

    pair_vm_t vm = view_model_pair_build(&net, &spotify, a->port, a->listener_running, a->was_pairing);
    ui_router_update_overlay(a->router, &vm);
}
