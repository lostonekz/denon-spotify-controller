/*
 * svc_denon_create() and the runtime setters. Target-only -- see
 * denon_service.h's top comment. The actual host-tested "create" logic is
 * denon_service_state_init(), called from here as one step among several
 * (transport wiring, real actions install, task+mutex creation).
 */
#include "svc_denon_internal.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "svc_denon";

/* EVENTS_AND_STATE.md §6.4: "every transition is logged... this log is the
 * entire debugging story" -- the same worked example
 * ("[fsm:denon_link] CONNECTED --(EVT_SOCKET_EOF)--> BACKOFF") core_fsm.h's
 * own header comment uses. app_wiring passes a pointer to this via
 * cfg.state_cfg.link_fsm_log; NULL in host tests (denon_service_state.h's
 * cfg contract). */
static const char *denon_link_state_name(uint32_t s)
{
    switch ((denon_link_state_t)s) {
    case DENON_LINK_STATE_IDLE:               return "IDLE";
    case DENON_LINK_STATE_DISCOVERING:        return "DISCOVERING";
    case DENON_LINK_STATE_CONNECTING_TELNET:  return "CONNECTING_TELNET";
    case DENON_LINK_STATE_CONNECTED_TELNET:   return "CONNECTED_TELNET";
    case DENON_LINK_STATE_CONNECTING_HTTP:    return "CONNECTING_HTTP";
    case DENON_LINK_STATE_CONNECTED_HTTP:     return "CONNECTED_HTTP";
    case DENON_LINK_STATE_BACKOFF_DISCOVERY:  return "BACKOFF_DISCOVERY";
    case DENON_LINK_STATE_BACKOFF_CONNECT:    return "BACKOFF_CONNECT";
    case DENON_LINK_STATE_RELEASED:           return "RELEASED";
    default:                                 return "?";
    }
}

static const char *denon_link_event_name(uint32_t e)
{
    switch ((denon_link_event_t)e) {
    case DENON_LINK_EVT_HOST_CONFIGURED:  return "HOST_CONFIGURED";
    case DENON_LINK_EVT_DISCOVERY_NEEDED: return "DISCOVERY_NEEDED";
    case DENON_LINK_EVT_DISCOVERED:       return "DISCOVERED";
    case DENON_LINK_EVT_DISCOVERY_FAILED: return "DISCOVERY_FAILED";
    case DENON_LINK_EVT_TELNET_CONNECTED: return "TELNET_CONNECTED";
    case DENON_LINK_EVT_TELNET_REFUSED:   return "TELNET_REFUSED";
    case DENON_LINK_EVT_TELNET_EOF:       return "TELNET_EOF";
    case DENON_LINK_EVT_HTTP_CONNECTED:   return "HTTP_CONNECTED";
    case DENON_LINK_EVT_HTTP_FAILED:      return "HTTP_FAILED";
    case DENON_LINK_EVT_HOST_UNREACHABLE: return "HOST_UNREACHABLE";
    case DENON_LINK_EVT_PROMOTE_DUE:      return "PROMOTE_DUE";
    case DENON_LINK_EVT_BACKOFF_ELAPSED:  return "BACKOFF_ELAPSED";
    case DENON_LINK_EVT_SLEEP_ENTER:      return "SLEEP_ENTER";
    case DENON_LINK_EVT_WAKE:             return "WAKE";
    default:                             return "?";
    }
}

void denon_link_fsm_transition_logger(void *log_ctx, const char *fsm_name,
                                        uint32_t from_state, uint32_t event, uint32_t to_state)
{
    (void)log_ctx;
    ESP_LOGI(TAG, "[fsm:%s] %s --(%s)--> %s", fsm_name,
              denon_link_state_name(from_state), denon_link_event_name(event), denon_link_state_name(to_state));
}

bool svc_denon_create(svc_denon_t *d, const svc_denon_cfg_t *cfg)
{
    memset(d, 0, sizeof(*d));

    d->http        = cfg->http;
    d->bus         = cfg->bus;
    d->state_store = cfg->state_store;
    d->clock       = cfg->clock;
    d->http_policy = cfg->http_policy;

    if (!denon_service_state_init(&d->state, &cfg->state_cfg)) {
        return false;
    }

    d->telnet_iface = denon_telnet_transport(&d->telnet_ctx);
    d->http_iface   = denon_http_transport(&d->http_ctx, d->http, &d->http_policy);

    /* Direct field assignment, not a re-call of denon_service_state_init()
     * -- see that function's own comment for why re-init would be wrong
     * here (it would reset current_state back to initial_state, exactly
     * what must not happen to a service that goes on to actually run). */
    denon_link_fsm_actions_t real = denon_service_link_real_actions();
    real.ctx                 = d;
    d->state.link_fsm.actions = real;

    d->cmdq_lock = xSemaphoreCreateMutex();
    if (d->cmdq_lock == NULL) {
        return false;
    }
    d->stopped_sem = xSemaphoreCreateBinary();
    if (d->stopped_sem == NULL) {
        vSemaphoreDelete(d->cmdq_lock);
        d->cmdq_lock = NULL;
        return false;
    }

    d->task_should_stop = false;
    BaseType_t ok = xTaskCreatePinnedToCore(denon_service_task, "denon", DENON_TASK_STACK, d,
                                              DENON_TASK_PRIO, &d->task_handle, DENON_TASK_CORE);
    if (ok != pdPASS) {
        vSemaphoreDelete(d->cmdq_lock);
        vSemaphoreDelete(d->stopped_sem);
        d->cmdq_lock   = NULL;
        d->stopped_sem = NULL;
        return false;
    }
    return true;
}

denon_link_state_t svc_denon_link_state(const svc_denon_t *d)
{
    return denon_link_fsm_current_state(&d->state.link_fsm);
}

void svc_denon_set_transport_pref(svc_denon_t *d, denon_link_transport_pref_t pref)
{
    d->state.transport_pref = pref;
}

void svc_denon_set_volume_limit(svc_denon_t *d, uint16_t limit_half_db)
{
    d->state.volume_limit_half_db = limit_half_db;
}

void svc_denon_set_host(svc_denon_t *d, const char *host)
{
    if (host == NULL) {
        return;
    }
    strncpy(d->state.host, host, sizeof(d->state.host) - 1);
    d->state.host[sizeof(d->state.host) - 1] = '\0';

    /* Doesn't dispatch into the FSM directly -- this may run on a
     * different task than the denon task that owns the FSM, and the FSM
     * has no lock of its own (single-owner-task discipline, same as every
     * other *_fsm_t in this codebase, instead of a lock). The denon
     * task's own loop notices this flag and dispatches HOST_CONFIGURED
     * itself, only if still IDLE -- an active connection to a different
     * host is not forcibly disrupted by a settings edit, and the FSM
     * never returns to IDLE once left, so a host change after the initial
     * boot window is a known, accepted scope trim, not silently dropped. */
    d->state.host_changed_pending = true;
}
