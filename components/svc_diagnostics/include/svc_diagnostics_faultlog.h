/*
 * Pure rolling-last-8-fault bookkeeping. docs/LIFECYCLE.md §2.1: the fault
 * summary ("task, PC, reason, uptime, heap/stack watermarks, last 8 fault
 * records") is the low-sensitivity, structured artifact -- safe to persist
 * and, later, export -- as opposed to the raw coredump.
 *
 * Built only against kv_store_if_t -- host-testable against
 * test/host/fake_kv_store.c, same as svc_settings_migration.
 */
#ifndef SVC_DIAGNOSTICS_FAULTLOG_H
#define SVC_DIAGNOSTICS_FAULTLOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kv_store_if.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAULTLOG_MAX_RECORDS   8
#define FAULTLOG_TASK_NAME_MAX 16   /* matches esp_core_dump_summary_t's exc_task[16] */

#define FAULT_REASON_MANUAL    0   /* svc_diagnostics_record_fault() */
#define FAULT_REASON_COREDUMP  1   /* detected via esp_core_dump_image_check() on boot */

typedef struct {
    char     task_name[FAULTLOG_TASK_NAME_MAX];
    uint32_t pc;              /* 0 for a manually-recorded fault (no crash PC) */
    uint32_t reason;
    uint32_t uptime_s;
    uint32_t heap_free_min;
} fault_record_t;

/* Reads the persisted rolling log into out (oldest first), up to
 * out_capacity entries. Returns the number of records actually copied --
 * 0 if nothing has ever been persisted, which is a valid empty log, not an
 * error. */
size_t faultlog_load(kv_store_if_t *kv, fault_record_t *out, size_t out_capacity);

/* Appends `rec`, evicting the oldest record first once already at
 * FAULTLOG_MAX_RECORDS entries, and persists (and commits) the result.
 * False on a storage failure. */
bool faultlog_append(kv_store_if_t *kv, const fault_record_t *rec);

#ifdef __cplusplus
}
#endif

#endif /* SVC_DIAGNOSTICS_FAULTLOG_H */
