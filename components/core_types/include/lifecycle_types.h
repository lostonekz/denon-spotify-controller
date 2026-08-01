/*
 * Service descriptors read (never written) by svc_supervisor. docs/LIFECYCLE.md §1.
 *
 * svc_supervisor is a mechanism, not a policy owner: it executes a
 * declarative boot graph and restart table built from these descriptors. It
 * does not know anything service-specific -- that's declared here, in the
 * owning service's own header, and merely read by the supervisor.
 *
 * This lives in core_types, not in each service's own header, even though
 * LIFECYCLE.md's own example shows SPOTIFY_SERVICE_DESC declared inside
 * spotify_service.h: tools/arch_rules.json's svc layer has no self-edge
 * (services must not include each other's headers, same reasoning as UI
 * never including a service header), so svc_supervisor including a sibling
 * svc_*'s header would be a layering violation. Each service still declares
 * its own descriptor constant, using this shared type; only main/app_wiring.c
 * (app layer, allowed to depend on everything) actually collects them.
 *
 * service_desc_t is deliberately const-friendly (declared
 * `static const service_desc_t X = {...}` at the owning service, per the
 * LIFECYCLE.md example) -- it carries no live handle or status. Runtime
 * state (current handle, current status, restart attempt count) lives in
 * svc_supervisor's own internal table, indexed alongside the descriptors,
 * never inside them.
 */
#ifndef CORE_LIFECYCLE_TYPES_H
#define CORE_LIFECYCLE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SVC_ID_STORAGE = 0,
    SVC_ID_SETTINGS,
    SVC_ID_LOGGING,
    SVC_ID_DIAGNOSTICS,
    SVC_ID_SCHEDULER,
    SVC_ID_NET_WIFI,
    SVC_ID_NET_TIME,
    SVC_ID_NET_HTTP,
    SVC_ID_SPOTIFY,
    SVC_ID_DENON,
    SVC_ID_ARTWORK,
    SVC_ID_OTA,
    SVC_ID_POWER,
    SVC_ID_INPUT,
    SVC_ID_HAL_DISPLAY,       /* Phase 5 -- LIFECYCLE.md §1.1's restart table already
                                 * anticipated this row: reboot, degraded_ok=false */
    SVC_ID_HAL_TOUCH,
    SVC_ID_UI,
    SVC_ID_COUNT,            /* not a real service -- array-sizing sentinel */
} service_id_t;

/* SVC_ID_COUNT must stay <= 32 for this to fit a uint32_t requires mask. */
#define SVC_BIT(id) (1u << (uint32_t)(id))

typedef enum {
    SUBSYS_INIT = 0,
    SUBSYS_READY,
    SUBSYS_DEGRADED,          /* normal operating state, not an error (§1.1) */
    SUBSYS_FAILED,
} subsystem_status_t;

/*
 * LIFECYCLE.md §1.2: restart is mostly not attempted. A service may only be
 * restarted if its teardown is provably complete and idempotent -- where
 * that cannot be proven, reboot instead. Only svc_spotify/svc_denon/
 * svc_artwork are restartable at all (none exist as runtime services yet in
 * Phase 1); everything else is RESTART_REBOOT.
 */
typedef enum {
    RESTART_REBOOT = 0,             /* teardown not provably complete -- reboot the device */
    RESTART_IMMEDIATE,              /* teardown proven safe; restart in place, no backoff */
    RESTART_BACKOFF_THEN_DEGRADE,   /* restart with backoff; past max_restarts, degrade instead */
} restart_policy_t;

typedef void (*service_teardown_fn)(void *handle);

/* Re-creates the service from scratch using the construction context
 * captured at registration time; returns the new handle via *out_handle.
 * NULL when restart == RESTART_REBOOT -- those services are never asked to
 * restart in place. */
typedef bool (*service_restart_fn)(void *restart_ctx, void **out_handle);

typedef struct {
    service_id_t          id;
    const char            *name;
    uint32_t               requires;      /* SVC_BIT(...) OR'd together */
    restart_policy_t       restart;
    uint8_t                max_restarts;
    bool                   degraded_ok;   /* false: failure here is fatal, not degradable */
    service_teardown_fn    teardown;
    service_restart_fn     restart_fn;    /* NULL when restart == RESTART_REBOOT */
    void                  *restart_ctx;
} service_desc_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_LIFECYCLE_TYPES_H */
