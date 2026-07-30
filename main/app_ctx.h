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
#include "freertos/semphr.h"

#include "core_eventbus.h"
#include "core_state.h"
#include "kv_store_if.h"
#include "lifecycle_types.h"
#include "svc_diagnostics.h"
#include "svc_logging.h"
#include "svc_scheduler.h"
#include "svc_settings.h"
#include "svc_storage.h"
#include "svc_supervisor.h"

/* Sized for Phase 1's own subscriber count (zero -- no UI, no other
 * consumers yet) plus headroom for the next couple of phases' consumers.
 * Not a hard architectural limit; raise it when a real subscriber needs
 * more room. */
#define APP_EVENTBUS_MAX_SUBS       8
#define APP_EVENTBUS_TOPIC_POLICIES 2   /* only TOPIC_SETTINGS/TOPIC_DIAG are published in Phase 1 */

/* Every Phase 1 service that has a service_desc_t (storage, scheduler,
 * logging, settings, diagnostics). Grows as later phases add services. */
#define APP_SUPERVISOR_MAX_SERVICES 5

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

    /* --- lifecycle --- */
    service_desc_t       service_descs[APP_SUPERVISOR_MAX_SERVICES];
    svc_state_t           service_states[APP_SUPERVISOR_MAX_SERVICES];
    size_t                  service_count;
    svc_supervisor_t       supervisor;
    SemaphoreHandle_t        supervisor_lock_sem;
} app_ctx_t;

#endif /* APP_CTX_H */
