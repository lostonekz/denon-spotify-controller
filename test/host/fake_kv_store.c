#include "fake_kv_store.h"

#include <string.h>

static fake_kv_entry_t *find(fake_kv_store_t *store, const char *key)
{
    for (int i = 0; i < FAKE_KV_MAX_ENTRIES; i++) {
        if (store->entries[i].in_use && strcmp(store->entries[i].key, key) == 0) {
            return &store->entries[i];
        }
    }
    return NULL;
}

static fake_kv_entry_t *find_free(fake_kv_store_t *store)
{
    for (int i = 0; i < FAKE_KV_MAX_ENTRIES; i++) {
        if (!store->entries[i].in_use) {
            return &store->entries[i];
        }
    }
    return NULL;
}

static bool fake_get_blob(void *ctx, const char *key, void *out, size_t *inout_len)
{
    fake_kv_store_t *store = (fake_kv_store_t *)ctx;
    fake_kv_entry_t *e = find(store, key);
    if (e == NULL) {
        return false;
    }

    if (e->len > *inout_len) {
        *inout_len = e->len;
        return false;
    }

    memcpy(out, e->data, e->len);
    *inout_len = e->len;
    return true;
}

static bool fake_set_blob(void *ctx, const char *key, const void *data, size_t len)
{
    fake_kv_store_t *store = (fake_kv_store_t *)ctx;
    if (len > FAKE_KV_MAX_VALUE || strlen(key) >= FAKE_KV_MAX_KEY) {
        return false;
    }

    fake_kv_entry_t *e = find(store, key);
    if (e == NULL) {
        e = find_free(store);
        if (e == NULL) {
            return false;
        }
        e->in_use = true;
        /* strlen(key) < FAKE_KV_MAX_KEY already checked above. */
        memcpy(e->key, key, strlen(key) + 1);
    }

    memcpy(e->data, data, len);
    e->len = len;
    return true;
}

static bool fake_erase_key(void *ctx, const char *key)
{
    fake_kv_store_t *store = (fake_kv_store_t *)ctx;
    fake_kv_entry_t *e = find(store, key);
    if (e == NULL) {
        return false;
    }
    e->in_use = false;
    e->len = 0;
    return true;
}

static bool fake_commit(void *ctx)
{
    fake_kv_store_t *store = (fake_kv_store_t *)ctx;
    store->commit_calls++;
    return true;
}

void fake_kv_store_init(fake_kv_store_t *store)
{
    memset(store, 0, sizeof(*store));
}

kv_store_if_t fake_kv_store_get(fake_kv_store_t *store)
{
    kv_store_if_t kv = {
        .get_blob  = fake_get_blob,
        .set_blob  = fake_set_blob,
        .erase_key = fake_erase_key,
        .commit    = fake_commit,
        .ctx       = store,
    };
    return kv;
}
