/*
 * See app_wiring_ticks.h. Split out of app_wiring.c for the 400-line gate.
 */
#include "app_wiring_ticks.h"

#include <string.h>

#include "esp_heap_caps.h"            /* MALLOC_CAP_SPIRAM for the intent task's stack */
#include "esp_system.h"
#include "freertos/idf_additions.h"   /* xTaskCreatePinnedToCoreWithCaps() */

#include "app_wiring_volume.h"
#include "settings_intent_types.h"

void settings_tick_trampoline(void *ctx)    { svc_settings_tick((svc_settings_t *)ctx); }
void diagnostics_tick_trampoline(void *ctx) { svc_diagnostics_report_tick((svc_diagnostics_t *)ctx); }
void net_time_tick_trampoline(void *ctx)    { net_time_tick((net_time_t *)ctx); }
void svc_ota_tick_trampoline(void *ctx)     { svc_ota_tick((svc_ota_t *)ctx); }

/* The mediator LIFECYCLE.md §1.4 calls for: svc_supervisor owns the health-
 * gate verdict, svc_ota needs it, and svc has no self-edge -- so app_wiring
 * polls one and relays into the other. Also folds in the gate conditions
 * that need no dedicated event source (TLS-handshake-proven, heap-
 * watermark, and, since Phase 5 Tier 4, the UI's first rendered frame),
 * rather than giving each its own tick job. */
void health_gate_relay_tick_trampoline(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;

    svc_supervisor_health_gate_tick(&a->supervisor);

    svc_supervisor_report_health_condition(&a->supervisor, HEALTH_COND_TLS_HANDSHAKE,
                                             svc_ota_tls_handshake_proven(&a->svc_ota));

    /* PLATFORM.md §2.2: "panel initialised, UI rendered >= 1 frame, touch
     * enumerated." Init/enumeration already gate app_wiring_create_ui()
     * itself (a hal_display_init()/hal_touch_init() failure aborts boot
     * entirely) -- this is only the genuinely asynchronous part, re-polled
     * every tick until ui_app_has_rendered_frame() observes the flush
     * callback actually run, same idiom as TLS_HANDSHAKE/HEAP_WATERMARK
     * below. */
    svc_supervisor_report_health_condition(&a->supervisor, HEALTH_COND_PANEL_UI,
                                             ui_app_has_rendered_frame(&a->ui_app));

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
 * that routes by intent_domain_t. Not routed through core_eventbus -- see
 * APP_INTENT_QUEUE_DEPTH's comment in app_ctx.h for why intent_t needs its
 * own dedicated queue.
 *
 * A separate helper (not inlined into the tick function) so
 * INTENT_DOMAIN_MULTI can recurse into it exactly once with the resolved
 * single-domain intent -- app_wiring_resolve_multi_intent() only resolves,
 * it never submits, so this is the one and only place any intent actually
 * reaches a service. */
static void dispatch_one_intent(app_ctx_t *a, const intent_t *intent)
{
    switch (intent->domain) {
    case INTENT_DOMAIN_DENON:
        svc_denon_submit_intent(&a->svc_denon, intent);
        break;
    case INTENT_DOMAIN_PLAYBACK:
        svc_spotify_submit_intent(&a->svc_spotify, intent);
        break;
    case INTENT_DOMAIN_SETTINGS: {
        /* The Connection screen's volume-cap slider is the first real
         * submitter (INTENT_DOMAIN_SETTINGS existed as an enum value since
         * Phase 1 but had no payload shape or dispatch case until now).
         * app_wiring is app-layer and already links svc_settings directly
         * (no svc->svc edge needed, unlike svc_denon's own settings reads)
         * -- the existing TOPIC_SETTINGS -> svc_denon relay above picks up
         * the resulting change from here automatically, so this is the
         * only new wiring the write side needs. */
        if ((settings_intent_kind_t)intent->kind == SETTINGS_INTENT_SET_VOLUME_LIMIT) {
            settings_intent_payload_t payload;
            memcpy(&payload, intent->payload, sizeof(payload));
            settings_set_u8(&a->settings, SETTING_VOLUME_LIMIT_MAX, (uint8_t)payload.volume_limit_half_db);
        }
        break;
    }
    case INTENT_DOMAIN_MULTI: {
        intent_t resolved = app_wiring_resolve_multi_intent(a, intent);
        if (resolved.domain != INTENT_DOMAIN_COUNT) {
            dispatch_one_intent(a, &resolved);   /* bounded -- MULTI never resolves to MULTI */
        }
        break;
    }
    default:
        break;   /* no other domain has a real consumer yet */
    }
}

/* A dedicated task blocking on the queue, not a periodic scheduler job
 * polling it with a zero timeout.
 *
 * The polled version ran every 20ms -- 50 wakeups per second of the shared
 * svc_scheduler worker, each one almost always finding an empty queue,
 * since intents arrive only when a user actually touches a control. It
 * also put a 0-20ms latency floor under every tap, which is the opposite
 * of what the 20ms period was chosen to achieve (ROADMAP Phase 3's
 * sub-100ms feedback goal): blocking on the queue makes dispatch start the
 * instant an intent is posted.
 *
 * Never returns -- there is no app_wiring teardown path in this project
 * (the composition root lives for the life of the boot; a service restart
 * goes through svc_supervisor, which never tears down app_ctx itself), so
 * this deliberately has no stop flag to go with it. The same is true of
 * svc_scheduler's own worker task, which this replaces a job on. */
static void intent_dispatch_task(void *arg)
{
    app_ctx_t *a = (app_ctx_t *)arg;
    for (;;) {
        intent_t intent;
        if (xQueueReceive(a->intent_queue, &intent, portMAX_DELAY) == pdTRUE) {
            dispatch_one_intent(a, &intent);
        }
    }
}

bool app_wiring_start_intent_dispatch_task(app_ctx_t *ctx)
{
    /* Priority 5 matches `denon`/`spotify` (ARCHITECTURE.md §4) -- this
     * task exists only to hand an intent to one of those two services'
     * queues, so running below them would just add a scheduling hop
     * between a tap and the service that acts on it. Core 0 with the rest
     * of the non-UI work, keeping core 1 for the UI.
     *
     * 3 KB: this task only ever does a queue receive, a switch, a
     * core_state_read() (the MULTI resolve path) and another queue send --
     * no TLS, no NVS, no printf-family formatting, none of the deep
     * ESP-IDF call chains that made svc_scheduler's own 3 KB too small
     * once OTA-apply actions landed on it (see svc_scheduler.c's comment).
     * svc_diagnostics reports this task's own high-water mark under the
     * name below, so the assumption is checkable rather than asserted. */
    /* PSRAM-backed stack, via xTaskCreatePinnedToCoreWithCaps() -- the same
     * choice net_sup/denon/spotify/artwork/ui_app all make, and for the
     * same documented reason (MEMORY.md §1): internal SRAM is the resource
     * this whole design is rationed against, and there is very little of it
     * spare. A first version of this task used plain
     * xTaskCreatePinnedToCore(), which takes its stack from internal RAM --
     * ~3 KiB out of the ~24 KiB that is actually free at runtime, i.e. an
     * unforced ~12% cut to the scarcest resource on the board, in a system
     * where a single artwork fetch needs a 16.7 KiB contiguous internal
     * allocation for its TLS record buffer. The TCB still lands in internal
     * RAM automatically (idf_additions.h's guarantee); only the stack moves. */
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(intent_dispatch_task, "intent", 3072, ctx,
                                                       5, &ctx->intent_task_handle, 0,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return ok == pdPASS;
}

/* Phase 4 plan §8: verified directly against svc_ota_download.c/svc_ota.c
 * that these are the only two events svc_ota ever actually publishes
 * (OTA_EVT_AVAILABLE/OTA_EVT_APPLIED are defined but never published) --
 * no new OTA-side publish path was needed for this relay. */
void ota_artwork_relay_tick_trampoline(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    event_t evt;
    while (core_eventbus_poll(&a->bus, a->ota_artwork_relay_sub, &evt)) {
        if (evt.topic != TOPIC_OTA) {
            continue;
        }
        if (evt.u.ota.kind == OTA_EVT_PROGRESS && !a->artwork_suspended_for_ota) {
            a->artwork_suspended_for_ota = true;
            /* Requests suspend rather than calling it directly -- this
             * trampoline runs on a different task than "artwork"'s own,
             * and svc_artwork_suspend()/resume() are task-owned since
             * Phase 5 Tier -1 (see artwork_service.h's top comment: a
             * cross-task call here could free jpeg_staging out from under
             * an in-flight fetch/decode on the artwork task). */
            svc_artwork_request_suspend(&a->svc_artwork);
        } else if (evt.u.ota.kind == OTA_EVT_FAILED && a->artwork_suspended_for_ota) {
            a->artwork_suspended_for_ota = false;
            svc_artwork_request_resume(&a->svc_artwork);
        }
        /* OTA_EVT_APPLIED never actually fires -- applying reboots the
         * device via esp_restart() first, so svc_artwork simply starts
         * fresh on the next boot; no explicit resume-on-success path
         * needed here. */
    }
}
