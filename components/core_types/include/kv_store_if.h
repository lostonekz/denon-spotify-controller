/*
 * Blob key-value store, injected as a vtable. docs/adr/0002 lists this as
 * one of the DI seams with more than one implementation: NVS-backed
 * (svc_storage.c, target-only) or an in-memory fake (test/host/fake_kv_store.c),
 * so anything built against kv_store_if_t -- svc_settings, svc_diagnostics'
 * fault log -- is host-testable without ESP-IDF.
 *
 * Lives in core_types rather than svc_storage: arch_rules.json's svc layer
 * has no self-edge (services must not include each other's headers), so any
 * sibling svc_* component that needs a kv store has to depend on the
 * interface itself, not on svc_storage's own component. Same reasoning as
 * lifecycle_types.h's service_desc_t.
 *
 * Bound to one namespace at a time: whoever opens the store (svc_storage)
 * decides which namespace `ctx` refers to; this header has no notion of
 * namespaces itself.
 */
#ifndef KV_STORE_IF_H
#define KV_STORE_IF_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NVS's own limit (15 bytes, not counting the NUL) -- the real
 * implementation cannot exceed this regardless of caller, so it's declared
 * here rather than left to be discovered as a runtime failure. */
#define KV_KEY_MAX 15

typedef struct {
    /* On entry, *inout_len is the capacity of `out`. On success, *inout_len
     * is the blob's actual size (<= capacity). On failure due to the blob
     * being larger than the buffer, *inout_len is set to the real size (so
     * the caller can retry with a big-enough buffer) and this returns
     * false. False, *inout_len untouched, if the key does not exist. */
    bool (*get_blob)(void *ctx, const char *key, void *out, size_t *inout_len);

    /* Writes/overwrites a blob under key. Does not implicitly commit. */
    bool (*set_blob)(void *ctx, const char *key, const void *data, size_t len);

    /* False if the key did not exist. */
    bool (*erase_key)(void *ctx, const char *key);

    /* Flushes pending writes to flash. */
    bool (*commit)(void *ctx);

    void *ctx;
} kv_store_if_t;

#ifdef __cplusplus
}
#endif

#endif /* KV_STORE_IF_H */
