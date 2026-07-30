#include "svc_diagnostics_faultlog.h"

#include <string.h>

#define FAULTLOG_KEY "fault_log"

typedef struct {
    fault_record_t records[FAULTLOG_MAX_RECORDS];
    uint8_t          count;   /* how many of records[] are valid */
    uint8_t          head;    /* index of the oldest valid record */
} faultlog_store_t;

static bool load_store(kv_store_if_t *kv, faultlog_store_t *out)
{
    size_t len = sizeof(*out);
    if (!kv->get_blob(kv->ctx, FAULTLOG_KEY, out, &len) || len != sizeof(*out)) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

size_t faultlog_load(kv_store_if_t *kv, fault_record_t *out, size_t out_capacity)
{
    faultlog_store_t store;
    load_store(kv, &store);

    size_t n = (store.count < out_capacity) ? store.count : out_capacity;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (store.head + i) % FAULTLOG_MAX_RECORDS;
        out[i] = store.records[idx];
    }
    return n;
}

bool faultlog_append(kv_store_if_t *kv, const fault_record_t *rec)
{
    faultlog_store_t store;
    load_store(kv, &store);

    size_t write_idx = (store.head + store.count) % FAULTLOG_MAX_RECORDS;
    store.records[write_idx] = *rec;

    if (store.count < FAULTLOG_MAX_RECORDS) {
        store.count++;
    } else {
        store.head = (uint8_t)((store.head + 1) % FAULTLOG_MAX_RECORDS);   /* evict oldest */
    }

    if (!kv->set_blob(kv->ctx, FAULTLOG_KEY, &store, sizeof(store))) {
        return false;
    }
    return kv->commit(kv->ctx);
}
