/*
 * Pure schema_version migration chain. docs/PLATFORM.md §3.3: "A
 * schema_version key is read at boot and a migration chain runs
 * v1->v2->v3..., each step a small pure function with a host test."
 *
 * No FreeRTOS, no ESP-IDF -- built only against kv_store_if_t, so this is
 * directly host-testable against test/host/fake_kv_store.c, same as
 * core_fsm.
 */
#ifndef SVC_SETTINGS_MIGRATION_H
#define SVC_SETTINGS_MIGRATION_H

#include <stddef.h>
#include <stdint.h>

#include "kv_store_if.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_SCHEMA_VERSION_CURRENT 1
#define SETTINGS_SCHEMA_VERSION_KEY     "schema_version"

/* Mutates kv in place for one version step (from_version -> from_version + 1).
 * Returns false to abort the chain without advancing the persisted version
 * -- the next boot retries from the same version. */
typedef bool (*settings_migration_step_fn)(kv_store_if_t *kv);

typedef struct {
    uint16_t                    from_version;
    settings_migration_step_fn  apply;
} settings_migration_step_t;

typedef enum {
    SETTINGS_MIGRATE_OK = 0,
    /* Stored schema_version is newer than SETTINGS_SCHEMA_VERSION_CURRENT --
     * this firmware is older than the data (a rollback). Nothing is
     * modified; the caller must fall back to compiled-in defaults for every
     * setting rather than risk misreading a newer format. */
    SETTINGS_MIGRATE_ROLLBACK,
    /* No step was found for the stored version, or a step returned false.
     * Nothing beyond the last successful step was persisted. */
    SETTINGS_MIGRATE_STEP_FAILED,
} settings_migrate_result_t;

/* Reads schema_version from kv. Missing key means a fresh device -- nothing
 * to migrate from, so this just writes SETTINGS_SCHEMA_VERSION_CURRENT and
 * returns SETTINGS_MIGRATE_OK. Otherwise walks `steps` in version order
 * until schema_version == SETTINGS_SCHEMA_VERSION_CURRENT, persisting
 * (and committing) the version after every successful step. */
settings_migrate_result_t settings_migration_run(kv_store_if_t *kv,
                                                    const settings_migration_step_t *steps,
                                                    size_t steps_count);

#ifdef __cplusplus
}
#endif

#endif /* SVC_SETTINGS_MIGRATION_H */
