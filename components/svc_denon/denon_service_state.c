/*
 * See denon_service_state.h. Pure: no ESP-IDF, no I/O, no allocation.
 * Host-tested (test/host/test_denon_service_state.c).
 */
#include "denon_service_state.h"

#include <string.h>

bool denon_service_state_init(denon_service_state_t *s, const denon_service_state_cfg_t *cfg)
{
    memset(s, 0, sizeof(*s));

    strncpy(s->host, cfg->initial_host, sizeof(s->host) - 1);
    s->host[sizeof(s->host) - 1] = '\0';
    s->transport_pref       = cfg->transport_pref;
    s->volume_limit_half_db = cfg->volume_limit_half_db;

    denon_cmdq_init(&s->cmdq);

    /* All-NULL actions -- inert, no I/O possible. The real vtable
     * (denon_service_link.c, target-only) is installed by svc_denon_create()
     * via a direct field assignment on s->link_fsm.actions, not by
     * re-calling denon_link_fsm_init(), which would reset current_state. */
    denon_link_fsm_actions_t noop_actions = { 0 };
    /* Always IDLE, never CONNECTING_TELNET directly: core_fsm_init() only sets
     * current_state, it never runs a transition's action -- landing directly in
     * CONNECTING_TELNET with a cached host would never actually call
     * telnet_iface.open() (nothing dispatches an event from that state on its
     * own). A cached host instead sets host_changed_pending, which the drive
     * loop's existing IDLE branch turns into a real HOST_CONFIGURED dispatch
     * (denon_service_link.c), the same path a live settings edit already uses. */
    denon_link_fsm_init(&s->link_fsm, DENON_LINK_STATE_IDLE, &noop_actions,
                          cfg->link_fsm_log, cfg->link_fsm_log_ctx);
    s->host_changed_pending = (s->host[0] != '\0');

    return true;
}

void denon_service_state_reset(denon_service_state_t *s)
{
    memset(s, 0, sizeof(*s));
}
