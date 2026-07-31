/*
 * denon_link_fsm's real actions (target-only: opens sockets, makes HTTP
 * calls) plus the drive loop that turns each action's synchronous result
 * into a follow-up dispatch. See denon_service.h's top comment.
 *
 * core_fsm_dispatch() runs an action BEFORE updating current_state, so an
 * action cannot safely call denon_link_fsm_dispatch() itself (the nested
 * call would look up the OLD state, not the one being transitioned into).
 * Every action here instead performs its I/O synchronously and stores the
 * result in a "just_ran"/"ok" flag pair on svc_denon_t's embedded `state`
 * (denon_service_state.h); denon_service_link_drive() -- called every
 * iteration of the denon task's own loop, unconditionally -- notices a
 * set flag and dispatches the real follow-up event from OUTSIDE any
 * action, the same "chain after dispatch returns, not from within"
 * discipline net_wifi_fsm_dispatch() uses for its own
 * DEGRADED->RECONNECTING auto-chain.
 *
 * denon_link_transport_pref_t ("force telnet"/"force http"/auto) is implemented
 * here, not as new FSM states/events: forcing HTTP makes
 * action_start_connect_telnet report failure without ever attempting a
 * real connect; forcing TELNET does the same to action_start_connect_http.
 * The FSM's own transition table (immediate fallback, backoff, promotion)
 * still runs unmodified either way -- "force http" just means every telnet
 * attempt looks refused, so BACKOFF_CONNECT's own retry-telnet-then-fall-
 * back-to-http-again loop naturally keeps landing back on HTTP.
 */
#include "svc_denon_internal.h"

#include "denon_discovery.h"
#include "denon_http.h"

#include "esp_random.h"

#define DENON_DISCOVERY_TIMEOUT_MS   5000u
#define DENON_LINK_BACKOFF_US        ((int64_t)5 * 1000000)    /* judgement call -- not specified in any doc */
#define DENON_LINK_PROMOTE_BASE_US   ((int64_t)60 * 1000000)   /* adr/0003 point 3's stated 60s */
#define DENON_LINK_PROMOTE_JITTER_US ((int64_t)10 * 1000000)   /* judgement call, "+jitter" is not quantified */

static void action_start_discovery(void *ctx)
{
    svc_denon_t *d = (svc_denon_t *)ctx;
    d->state.discovery_ok        = denon_discovery_run(d->state.host, sizeof(d->state.host),
                                                          DENON_DISCOVERY_TIMEOUT_MS);
    d->state.discovery_just_ran = true;
}

static void action_start_connect_telnet(void *ctx)
{
    svc_denon_t *d = (svc_denon_t *)ctx;
    d->state.telnet_open_ok = (d->state.transport_pref != DENON_LINK_TRANSPORT_HTTP) &&
                                d->telnet_iface.open(d->telnet_iface.ctx, d->state.host);
    d->state.telnet_open_just_ran = true;
}

static void action_probe_capabilities(void *ctx)
{
    svc_denon_t *d = (svc_denon_t *)ctx;
    /* Fire-and-forget -- RISKS.md R5's mitigation is detecting a changed
     * device via logging, not gating anything on the result. */
    denon_http_probe_capabilities(d->http, &d->http_policy, d->state.host);
}

static void action_enter_connected_telnet(void *ctx)
{
    (void)ctx;   /* state/event publishing happens uniformly in the task loop, not per-action */
}

static void action_start_connect_http(void *ctx)
{
    svc_denon_t *d = (svc_denon_t *)ctx;
    d->state.http_open_ok = (d->state.transport_pref != DENON_LINK_TRANSPORT_TELNET) &&
                              d->http_iface.open(d->http_iface.ctx, d->state.host);
    d->state.http_open_just_ran = true;
}

static void action_enter_connected_http(void *ctx)
{
    svc_denon_t *d = (svc_denon_t *)ctx;
    int64_t now = d->clock.now_us(d->clock.ctx);
    d->state.promote_deadline_us =
        now + DENON_LINK_PROMOTE_BASE_US + (int64_t)(esp_random() % (uint32_t)DENON_LINK_PROMOTE_JITTER_US);
}

static void action_start_backoff_discovery(void *ctx)
{
    svc_denon_t *d = (svc_denon_t *)ctx;
    d->state.backoff_deadline_us = d->clock.now_us(d->clock.ctx) + DENON_LINK_BACKOFF_US;
}

static void action_start_backoff_connect(void *ctx)
{
    svc_denon_t *d = (svc_denon_t *)ctx;
    d->state.backoff_deadline_us = d->clock.now_us(d->clock.ctx) + DENON_LINK_BACKOFF_US;
}

static void action_release_link(void *ctx)
{
    svc_denon_t *d = (svc_denon_t *)ctx;
    d->telnet_iface.close(d->telnet_iface.ctx);
    d->http_iface.close(d->http_iface.ctx);
}

denon_link_fsm_actions_t denon_service_link_real_actions(void)
{
    denon_link_fsm_actions_t actions = {
        .start_discovery          = action_start_discovery,
        .start_connect_telnet     = action_start_connect_telnet,
        .probe_capabilities       = action_probe_capabilities,
        .enter_connected_telnet   = action_enter_connected_telnet,
        .start_connect_http       = action_start_connect_http,
        .enter_connected_http     = action_enter_connected_http,
        .start_backoff_discovery  = action_start_backoff_discovery,
        .start_backoff_connect    = action_start_backoff_connect,
        .release_link             = action_release_link,
        .ctx                      = NULL,   /* caller (svc_denon_create) fills this in with d */
    };
    return actions;
}

void denon_service_link_drive(svc_denon_t *d)
{
    bool changed = true;
    while (changed) {
        changed = false;

        if (d->state.discovery_just_ran) {
            d->state.discovery_just_ran = false;
            denon_link_fsm_dispatch(&d->state.link_fsm,
                d->state.discovery_ok ? DENON_LINK_EVT_DISCOVERED : DENON_LINK_EVT_DISCOVERY_FAILED, NULL);
            changed = true;
            continue;
        }
        if (d->state.telnet_open_just_ran) {
            d->state.telnet_open_just_ran = false;
            denon_link_fsm_dispatch(&d->state.link_fsm,
                d->state.telnet_open_ok ? DENON_LINK_EVT_TELNET_CONNECTED : DENON_LINK_EVT_TELNET_REFUSED, NULL);
            changed = true;
            continue;
        }
        if (d->state.http_open_just_ran) {
            d->state.http_open_just_ran = false;
            denon_link_fsm_dispatch(&d->state.link_fsm,
                d->state.http_open_ok ? DENON_LINK_EVT_HTTP_CONNECTED : DENON_LINK_EVT_HTTP_FAILED, NULL);
            changed = true;
            continue;
        }

        denon_link_state_t s = denon_link_fsm_current_state(&d->state.link_fsm);

        /* Never returns to IDLE once left -- see svc_denon_set_host()'s
         * comment. A settings edit after the initial boot window is a
         * known, accepted scope trim, not silently dropped. */
        if (d->state.host_changed_pending && s == DENON_LINK_STATE_IDLE) {
            d->state.host_changed_pending = false;
            denon_link_fsm_dispatch(&d->state.link_fsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
            changed = true;
            continue;
        }

        /* Booted with no cached host (denon_service_state_init() picked
         * IDLE) -- nothing else ever fires DISCOVERY_NEEDED, so without
         * this the FSM would sit in IDLE forever. IDLE is only ever the
         * boot-time state (never re-entered), so this only ever fires
         * once per boot. */
        if (s == DENON_LINK_STATE_IDLE && !d->state.host_changed_pending) {
            denon_link_fsm_dispatch(&d->state.link_fsm, DENON_LINK_EVT_DISCOVERY_NEEDED, NULL);
            changed = true;
            continue;
        }

        int64_t now = d->clock.now_us(d->clock.ctx);
        if ((s == DENON_LINK_STATE_BACKOFF_DISCOVERY || s == DENON_LINK_STATE_BACKOFF_CONNECT) &&
            now >= d->state.backoff_deadline_us) {
            denon_link_fsm_dispatch(&d->state.link_fsm, DENON_LINK_EVT_BACKOFF_ELAPSED, NULL);
            changed = true;
            continue;
        }
        if (s == DENON_LINK_STATE_CONNECTED_HTTP && now >= d->state.promote_deadline_us) {
            denon_link_fsm_dispatch(&d->state.link_fsm, DENON_LINK_EVT_PROMOTE_DUE, NULL);
            changed = true;
            continue;
        }
    }
}
