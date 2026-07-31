/*
 * See app_wiring_ticks.h. Split out of app_wiring.c for the 400-line gate.
 */
#include "app_wiring_ticks.h"

#include "esp_system.h"

void settings_tick_trampoline(void *ctx)    { svc_settings_tick((svc_settings_t *)ctx); }
void diagnostics_tick_trampoline(void *ctx) { svc_diagnostics_report_tick((svc_diagnostics_t *)ctx); }
void net_time_tick_trampoline(void *ctx)    { net_time_tick((net_time_t *)ctx); }
void svc_ota_tick_trampoline(void *ctx)     { svc_ota_tick((svc_ota_t *)ctx); }

/* The mediator LIFECYCLE.md §1.4 calls for: svc_supervisor owns the health-
 * gate verdict, svc_ota needs it, and svc has no self-edge -- so app_wiring
 * polls one and relays into the other. Also folds in the two gate
 * conditions that need no dedicated event source (TLS-handshake-proven,
 * heap-watermark), rather than giving each its own tick job. */
void health_gate_relay_tick_trampoline(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;

    svc_supervisor_health_gate_tick(&a->supervisor);

    svc_supervisor_report_health_condition(&a->supervisor, HEALTH_COND_TLS_HANDSHAKE,
                                             svc_ota_tls_handshake_proven(&a->svc_ota));

    uint32_t baseline = svc_ota_heap_watermark_baseline(&a->svc_ota);
    uint32_t current    = esp_get_free_heap_size();
    /* No baseline yet (first-ever boot, nothing confirmed to regress
     * against) reads as ok -- there is nothing to have leaked relative to. */
    bool heap_ok = (baseline == 0) || ((uint64_t)current * 100u >= (uint64_t)baseline * 85u);
    svc_supervisor_report_health_condition(&a->supervisor, HEALTH_COND_HEAP_WATERMARK, heap_ok);

    health_gate_verdict_t v = svc_supervisor_health_gate_verdict(&a->supervisor);
    if (v == HEALTH_GATE_PASS) {
        svc_ota_apply_gate_event(&a->svc_ota, OTA_FSM_EVT_GATE_PASS);
    } else if (v == HEALTH_GATE_TIMED_OUT) {
        svc_ota_apply_gate_event(&a->svc_ota, OTA_FSM_EVT_GATE_TIMED_OUT);
    }
}

/* svc_settings.h's denon_transport_pref_t and denon_service_state.h's
 * denon_link_transport_pref_t are two independently-declared enums with
 * the same 3 values in the same order (svc_denon can't #include
 * svc_settings.h to share one -- no svc->svc edge) -- an explicit
 * conversion here, not a raw cast, so a future divergence between the two
 * is a compile-time switch-exhaustiveness issue to notice, not a silent
 * numeric coincidence. */
static denon_link_transport_pref_t denon_transport_pref_from_settings(uint8_t v)
{
    switch ((denon_transport_pref_t)v) {
    case DENON_TRANSPORT_TELNET: return DENON_LINK_TRANSPORT_TELNET;
    case DENON_TRANSPORT_HTTP:   return DENON_LINK_TRANSPORT_HTTP;
    case DENON_TRANSPORT_AUTO:
    default:                     return DENON_LINK_TRANSPORT_AUTO;
    }
}

/* Third instance of the mediator-trampoline pattern
 * (health_gate_relay_tick_trampoline above is the second): svc_denon
 * cannot call svc_settings itself (no svc->svc edge), so app_wiring owns
 * this TOPIC_SETTINGS subscription and relays only the Denon-relevant
 * keys via svc_denon's setters. Event-driven, not polled -- svc_settings.c
 * already publishes a real event on every settings_set_*() call; draining
 * it here costs nothing when nothing Denon-relevant changed. */
void denon_settings_relay_tick_trampoline(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    event_t evt;
    while (core_eventbus_poll(&a->bus, a->denon_settings_sub, &evt)) {
        if (evt.topic != TOPIC_SETTINGS) {
            continue;
        }
        switch ((setting_key_t)evt.u.setting.key) {
        case SETTING_DENON_HOST: {
            char host[SETTING_STR_MAX];
            if (settings_get_str(&a->settings, SETTING_DENON_HOST, host)) {
                svc_denon_set_host(&a->svc_denon, host);
            }
            break;
        }
        case SETTING_DENON_TRANSPORT_PREF: {
            uint8_t v;
            if (settings_get_u8(&a->settings, SETTING_DENON_TRANSPORT_PREF, &v)) {
                svc_denon_set_transport_pref(&a->svc_denon, denon_transport_pref_from_settings(v));
            }
            break;
        }
        case SETTING_VOLUME_LIMIT_MAX: {
            uint8_t v;
            if (settings_get_u8(&a->settings, SETTING_VOLUME_LIMIT_MAX, &v)) {
                svc_denon_set_volume_limit(&a->svc_denon, (uint16_t)v);
            }
            break;
        }
        default:
            break;   /* not a Denon-relevant setting */
        }
    }
}

/* EVENTS_AND_STATE.md §5.1's single intent queue, drained by a dispatcher
 * that routes by intent_domain_t -- one real case today
 * (INTENT_DOMAIN_DENON); Phase 4/5 add a case, not a rewrite. Not routed
 * through core_eventbus -- see APP_INTENT_QUEUE_DEPTH's comment in
 * app_ctx.h for why intent_t needs its own dedicated queue. */
void intent_dispatch_tick_trampoline(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    intent_t intent;
    while (xQueueReceive(a->intent_queue, &intent, 0) == pdTRUE) {
        switch (intent.domain) {
        case INTENT_DOMAIN_DENON:
            svc_denon_submit_intent(&a->svc_denon, &intent);
            break;
        default:
            break;   /* no other domain has a real consumer yet */
        }
    }
}
