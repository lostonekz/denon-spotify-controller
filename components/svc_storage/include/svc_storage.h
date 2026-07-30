/*
 * NVS-backed namespace store -- "the only module that touches NVS"
 * (docs/MODULES.md). Caller-owned storage, no malloc, same discipline as
 * core_pool/core_eventbus/core_state (ARCHITECTURE.md §6): a fixed number
 * of namespaces, each handed out as a kv_store_if_t bound to its own NVS
 * handle.
 *
 * Target-only: not host-tested (nvs_open/get_blob/etc. need real flash).
 * Anything that needs to be host-tested against a store built on top of
 * this (svc_settings, svc_diagnostics' fault log) is written against
 * kv_store_if_t, not against svc_storage.h directly.
 */
#ifndef SVC_STORAGE_H
#define SVC_STORAGE_H

#include <stdbool.h>

#include "nvs.h"

#include "kv_store_if.h"
#include "lifecycle_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NVS encryption is a whole-partition feature (nvs_flash_secure_init() +
 * the nvs_keys partition), not a per-namespace toggle, and the production
 * path that provisions those keys doesn't exist yet -- no eFuse operations
 * on dev units, ever (docs/adr/0006-staged-security.md). KV_NS_ENCRYPTED is
 * accepted now so callers (svc_settings' secrets namespace) don't need a
 * signature change later; until the secure-init path is built, it is a
 * documented no-op and behaves identically to KV_NS_PLAIN. */
typedef enum {
    KV_NS_PLAIN = 0,
    KV_NS_ENCRYPTED,
} kv_ns_kind_t;

#define SVC_STORAGE_MAX_NAMESPACES 8

typedef struct {
    nvs_handle_t handle;
    bool         in_use;
} svc_storage_ns_slot_t;

typedef struct {
    svc_storage_ns_slot_t namespaces[SVC_STORAGE_MAX_NAMESPACES];
} svc_storage_t;

/* Initializes the NVS flash partition, erasing and retrying once on the
 * documented ESP-IDF recovery cases (no free pages / format version
 * mismatch). Call once, before any svc_storage_open_namespace. */
bool svc_storage_create(svc_storage_t *s);

/* Opens (creating if needed) a namespace and fills `out` with a
 * kv_store_if_t bound to it. namespace_name must be <= 15 bytes. False if
 * the namespace table is full or nvs_open fails. */
bool svc_storage_open_namespace(svc_storage_t *s, const char *namespace_name,
                                 kv_ns_kind_t kind, kv_store_if_t *out);

/* Closes every open namespace handle. */
void svc_storage_destroy(svc_storage_t *s);

/* docs/LIFECYCLE.md §1.2: "NVS handle loss is unrecoverable in place" --
 * reboot, not restartable. degraded_ok = false: storage failing is fatal,
 * not a degraded operating condition (one of only two services the doc
 * calls out this way, alongside the display path). */
static const service_desc_t SVC_STORAGE_SERVICE_DESC = {
    .id           = SVC_ID_STORAGE,
    .name         = "storage",
    .requires     = 0,
    .restart      = RESTART_REBOOT,
    .max_restarts = 0,
    .degraded_ok  = false,
    .teardown     = NULL,
    .restart_fn   = NULL,
    .restart_ctx  = NULL,
};

#ifdef __cplusplus
}
#endif

#endif /* SVC_STORAGE_H */
