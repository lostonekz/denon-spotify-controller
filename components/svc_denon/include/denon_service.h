/*
 * The Denon service: owns the "denon" task (ARCHITECTURE.md §4: core 0,
 * prio 5, 6KB, "socket select + cmd queue", event-driven, blocking
 * allowed), the transports, and the mutex guarding the command queue.
 *
 * Target-only -- this header (and every .c file that includes it) is
 * never linked into a host test. See denon_service_state.h's top comment
 * for the reason (transitively embeds net_http_t*, which pulls in
 * FreeRTOS) and for what IS host-tested instead: the pure
 * denon_service_state_t this struct embeds as its `state` field.
 *
 * svc_denon cannot depend on svc_settings or svc_supervisor directly (no
 * svc->svc edge, tools/arch_rules.json) -- app_wiring is the mediator:
 * cfg carries an initial settings snapshot taken once at construction, and
 * runtime edits arrive later via svc_denon_set_*() setters that app_wiring
 * calls from its TOPIC_SETTINGS relay (docs/ROADMAP.md Phase 3's plan).
 */
#ifndef DENON_SERVICE_H
#define DENON_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "core_eventbus.h"
#include "core_state.h"
#include "intent_types.h"
#include "lifecycle_types.h"
#include "net_http.h"
#include "os_if_types.h"

#include "denon_http.h"
#include "denon_service_state.h"
#include "denon_telnet.h"
#include "denon_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    net_http_t         *http;
    core_eventbus_t       *bus;
    core_state_t             *state_store;
    time_source_t               clock;
    http_policy_t                  http_policy;

    denon_service_state_cfg_t state_cfg;   /* forwarded into denon_service_state_init() */
} svc_denon_cfg_t;

typedef struct {
    denon_service_state_t state;   /* the pure, host-tested part -- see denon_service_state.h */

    denon_telnet_ctx_t telnet_ctx;
    denon_http_ctx_t     http_ctx;
    denon_transport_if_t   telnet_iface;
    denon_transport_if_t     http_iface;

    net_http_t   *http;
    http_policy_t  http_policy;
    core_eventbus_t *bus;
    core_state_t      *state_store;
    time_source_t       clock;

    SemaphoreHandle_t cmdq_lock;   /* protects state.cmdq across the denon task (drains) and
                                     * whichever task calls svc_denon_submit_intent() */
    TaskHandle_t         task_handle;
    volatile bool           task_should_stop;
    SemaphoreHandle_t          stopped_sem;   /* denon_task gives this right before exiting */
} svc_denon_t;

/* Everything: pure state init, transport wiring, real actions vtable
 * install, cmdq_lock + task creation. Target-only -- see this header's
 * top comment for why there is no separate host-testable "create" step
 * here (denon_service_state_init() is that step, and it's already
 * directly host-tested on its own). */
bool svc_denon_create(svc_denon_t *d, const svc_denon_cfg_t *cfg);

/* service_teardown_fn: stops the task, frees cmdq_lock/stopped_sem, force-
 * closes both transports, resets state. */
void svc_denon_teardown(void *handle);

typedef struct {
    svc_denon_t     *d;
    svc_denon_cfg_t   cfg;   /* retained so a restart can recreate identically */
} svc_denon_restart_ctx_t;

/* service_restart_fn. */
bool svc_denon_restart(void *restart_ctx, void **out_handle);

/* Builds a service_desc_t for svc_denon. restart_ctx must outlive the
 * service (app_wiring owns a svc_denon_restart_ctx_t alongside svc_denon_t
 * itself) -- .restart_ctx cannot be baked into a compile-time constant the
 * way SVC_OTA_SERVICE_DESC is, since it must point at a real runtime
 * instance app_wiring constructs, not something knowable at compile time. */
service_desc_t svc_denon_service_desc(svc_denon_restart_ctx_t *restart_ctx);

/* Validates intent->domain == INTENT_DOMAIN_DENON, applies the volume
 * safety clamp for DENON_INTENT_SET_VOLUME (docs/RISKS.md R13 -- enforced
 * here, at the protocol boundary, never relied on upstream; see
 * denon_intent.c, which does the real work), and submits the resulting
 * wire command to cmdq (thread-safe via cmdq_lock). Returns false if the
 * intent is malformed, the wrong domain, or the discrete queue is full. */
bool svc_denon_submit_intent(svc_denon_t *d, const intent_t *intent);

/* SETTING_DENON_TRANSPORT_PREF / SETTING_VOLUME_LIMIT_MAX / SETTING_DENON_HOST
 * runtime setters -- called only by app_wiring's TOPIC_SETTINGS relay. */
void svc_denon_set_transport_pref(svc_denon_t *d, denon_link_transport_pref_t pref);
void svc_denon_set_volume_limit(svc_denon_t *d, uint16_t limit_half_db);
void svc_denon_set_host(svc_denon_t *d, const char *host);

denon_link_state_t svc_denon_link_state(const svc_denon_t *d);

/* EVENTS_AND_STATE.md §6.4's transition logger, matching fsm_transition_log_fn's
 * signature -- app_wiring passes this (or NULL, in host tests) as
 * svc_denon_cfg_t.state_cfg.link_fsm_log. */
void denon_link_fsm_transition_logger(void *log_ctx, const char *fsm_name,
                                        uint32_t from_state, uint32_t event, uint32_t to_state);

#ifdef __cplusplus
}
#endif

#endif /* DENON_SERVICE_H */
