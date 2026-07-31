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
    denon_link_state_t initial =
        (s->host[0] == '\0') ? DENON_LINK_STATE_IDLE : DENON_LINK_STATE_CONNECTING_TELNET;
    denon_link_fsm_init(&s->link_fsm, initial, &noop_actions, cfg->link_fsm_log, cfg->link_fsm_log_ctx);

    return true;
}

void denon_service_state_reset(denon_service_state_t *s)
{
    memset(s, 0, sizeof(*s));
}
