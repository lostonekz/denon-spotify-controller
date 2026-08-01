/*
 * The single static app_ctx_t holding every Phase 1 handle. docs/adr/0002:
 * "All handles are created in app_wiring.c in dependency order. That file
 * is the single place composite ownership exists." One instance, for the
 * life of the device -- ARCHITECTURE.md §6's fixed long-lived memory.
 */
#ifndef APP_CTX_H
#define APP_CTX_H

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "artwork_service.h"
#include "core_eventbus.h"
#include "core_state.h"
#include "denon_service.h"
#include "hal_display.h"
#include "hal_touch.h"
#include "intent_types.h"
#include "kv_store_if.h"
#include "lifecycle_types.h"
#include "net_http.h"
#include "net_time.h"
#include "net_wifi.h"
#include "spotify_service.h"
#include "svc_diagnostics.h"
#include "svc_logging.h"
#include "svc_ota.h"
#include "svc_scheduler.h"
#include "svc_settings.h"
#include "svc_storage.h"
#include "svc_supervisor.h"
#include "ui_app.h"

/* Sized for real subscriber count plus headroom for later phases. Phase 2
 * adds 3 real subscriptions (svc_supervisor on TOPIC_NET+TOPIC_TIME,
 * net_time on TOPIC_NET); Phase 3 adds 1 (app_wiring on TOPIC_SETTINGS, to
 * relay denon_host/denon_transport_pref/volume_step/volume_limit_max into
 * svc_denon -- svc_denon cannot call svc_settings itself, no svc->svc edge).
 * Phase 4 adds 2 more: svc_artwork's own TOPIC_PLAYBACK subscription (legal
 * svc->core edge, no mediation needed -- see EVENTS_AND_STATE.md's artwork
 * ownership contract) and app_wiring's TOPIC_OTA subscription for the
 * OTA<->artwork coexistence relay. Phase 5 Tier 6 adds 5 more, all owned by
 * view_model_now_playing_apply_t (a legal ui->core edge, same reasoning as
 * svc_artwork's own direct subscription): TOPIC_PLAYBACK/TOPIC_ARTWORK/
 * TOPIC_DENON/TOPIC_INTENT_ACK/TOPIC_TOAST. Not a hard architectural limit,
 * raise it when a real subscriber needs more room. */
#define APP_EVENTBUS_MAX_SUBS       14
/* TOPIC_SETTINGS/TOPIC_DIAG (Phase 1) + TOPIC_NET/TOPIC_TIME/TOPIC_OTA
 * (Phase 2) + TOPIC_DENON (Phase 3) + TOPIC_PLAYBACK/TOPIC_SPOTIFY/
 * TOPIC_ARTWORK/TOPIC_INTENT_ACK (Phase 4) + TOPIC_TOAST (Phase 5 Tier 6 --
 * w_toast's real consumer, view_model_now_playing_apply_t). */
#define APP_EVENTBUS_TOPIC_POLICIES 11

/* Every service that has a service_desc_t: Phase 1's storage/scheduler/
 * logging/settings/diagnostics (5) + Phase 2's net_wifi/net_time/net_http/
 * svc_ota (4) + Phase 3's svc_denon (1) + Phase 4's svc_spotify/svc_artwork
 * (2) + Phase 5's hal_display/hal_touch/ui (3). Grows as later phases add
 * services. */
#define APP_SUPERVISOR_MAX_SERVICES 15

/* EVENTS_AND_STATE.md §5.3's documented (but, until Phase 3, unbuilt)
 * TOPIC_INTENT policy: depth 16, drop newest + count it. intent_t does not
 * fit core_eventbus's 64 B event_t budget (it carries a 16 B payload plus a
 * 3-entry causal read_set on top of domain/kind/seq), and event_topic_t has
 * no TOPIC_INTENT member -- intents travel through this dedicated queue
 * instead, not through core_eventbus. */
#define APP_INTENT_QUEUE_DEPTH 16

typedef struct {
    /* --- core --- */
    core_eventbus_t   bus;
    eventbus_sub_t     bus_subs[APP_EVENTBUS_MAX_SUBS];
    topic_policy_t      bus_topic_policies[APP_EVENTBUS_TOPIC_POLICIES];
    SemaphoreHandle_t     bus_lock_sem;

    core_state_t       state;
    SemaphoreHandle_t     state_lock_sem;

    /* --- services --- */
    svc_storage_t      storage;
    kv_store_if_t        settings_kv;
    kv_store_if_t        diagnostics_kv;

    svc_scheduler_t    scheduler;
    svc_logging_t      logging;
    svc_settings_t     settings;
    svc_diagnostics_t  diagnostics;

    scheduler_job_h      settings_tick_job;
    scheduler_job_h      diagnostics_tick_job;

    /* --- net (Phase 2) --- */
    kv_store_if_t      secure_kv;   /* "secure" namespace: wifi.ssid/wifi.psk (PLATFORM.md §3.2) */
    net_wifi_t           net_wifi;
    net_time_t             net_time;
    net_http_t                net_http;
    scheduler_job_h             net_time_tick_job;

    /* --- ota (Phase 2) --- */
    kv_store_if_t      ota_kv;   /* "ota" namespace: sec_ver_max/heap_wm */
    svc_ota_t            svc_ota;
    scheduler_job_h         svc_ota_tick_job;
    scheduler_job_h           health_gate_relay_tick_job;

    /* --- intents (Phase 3): EVENTS_AND_STATE.md §5.1's single intent queue,
     * consumed by a dispatcher that routes by intent_domain_t. Depth-16,
     * drop-newest-and-count on overflow (intent_drop_count) -- see
     * APP_INTENT_QUEUE_DEPTH's comment for why this is a dedicated queue,
     * not a core_eventbus topic. --- */
    QueueHandle_t        intent_queue;
    scheduler_job_h         intent_dispatch_tick_job;
    uint32_t                  intent_drop_count;

    /* --- denon (Phase 3) --- */
    svc_denon_t          svc_denon;
    svc_denon_restart_ctx_t svc_denon_restart_ctx;
    eventbus_sub_h       denon_settings_sub;   /* TOPIC_SETTINGS, relayed into svc_denon */
    event_t                denon_settings_sub_storage[4];
    scheduler_job_h           denon_settings_relay_tick_job;

    /* --- spotify (Phase 4): no svc_settings dependency -- PLATFORM.md
     * §3.4, auth lives entirely in the secure namespace, not a typed
     * setting. --- */
    svc_spotify_t          svc_spotify;
    svc_spotify_restart_ctx_t svc_spotify_restart_ctx;

    /* --- artwork (Phase 4): owns its own TOPIC_PLAYBACK subscription
     * directly (svc_artwork_t's own eventbus_sub_h/storage, not app_ctx_t's
     * -- see artwork_service.h) -- app_ctx_t only needs the OTA-coexistence
     * relay's subscription below. --- */
    svc_artwork_t          svc_artwork;
    svc_artwork_restart_ctx_t svc_artwork_restart_ctx;
    eventbus_sub_h       ota_artwork_relay_sub;   /* TOPIC_OTA, relayed into svc_artwork suspend/resume */
    event_t                ota_artwork_relay_sub_storage[4];
    scheduler_job_h           ota_artwork_relay_tick_job;
    bool                        artwork_suspended_for_ota;

    /* --- ui (Phase 5): bsp_board itself owns no ctx-held handle (a
     * singleton behind bsp_board_i2c_bus()/bsp_board_io_expander(), safe to
     * call bsp_board_init() more than once -- bsp_board.h). hal_backlight
     * is likewise stateless (just the shared expander + a bool). ui_app_t
     * embeds ui_router_t itself (ui_app.h) -- no separate router field. --- */
    hal_display_t          hal_display;
    hal_touch_t               hal_touch;
    ui_app_t                    ui_app;

    /* --- lifecycle --- */
    service_desc_t       service_descs[APP_SUPERVISOR_MAX_SERVICES];
    svc_state_t           service_states[APP_SUPERVISOR_MAX_SERVICES];
    size_t                  service_count;
    svc_supervisor_t       supervisor;
    SemaphoreHandle_t        supervisor_lock_sem;
} app_ctx_t;

#endif /* APP_CTX_H */
