/*
 * In-memory kv_store_if_t for host tests: swaps in for svc_storage's real
 * NVS-backed implementation, same pattern as fake_lock/fake_time_source.
 */
#ifndef FAKE_KV_STORE_H
#define FAKE_KV_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kv_store_if.h"

#define FAKE_KV_MAX_ENTRIES 32
#define FAKE_KV_MAX_KEY     (KV_KEY_MAX + 1)
#define FAKE_KV_MAX_VALUE   512

typedef struct {
    char    key[FAKE_KV_MAX_KEY];
    uint8_t data[FAKE_KV_MAX_VALUE];
    size_t  len;
    bool    in_use;
} fake_kv_entry_t;

typedef struct {
    fake_kv_entry_t entries[FAKE_KV_MAX_ENTRIES];
    int             commit_calls;
} fake_kv_store_t;

void fake_kv_store_init(fake_kv_store_t *store);

/* The returned kv_store_if_t's ctx points at `store` -- store must outlive
 * it. */
kv_store_if_t fake_kv_store_get(fake_kv_store_t *store);

#endif /* FAKE_KV_STORE_H */
