#include "svc_settings_migration.h"

#include <stddef.h>

static bool read_version(kv_store_if_t *kv, uint16_t *out)
{
    size_t len = sizeof(*out);
    return kv->get_blob(kv->ctx, SETTINGS_SCHEMA_VERSION_KEY, out, &len) && len == sizeof(*out);
}

static bool write_version(kv_store_if_t *kv, uint16_t version)
{
    return kv->set_blob(kv->ctx, SETTINGS_SCHEMA_VERSION_KEY, &version, sizeof(version));
}

static const settings_migration_step_t *find_step(const settings_migration_step_t *steps,
                                                     size_t steps_count, uint16_t from_version)
{
    for (size_t i = 0; i < steps_count; i++) {
        if (steps[i].from_version == from_version) {
            return &steps[i];
        }
    }
    return NULL;
}

settings_migrate_result_t settings_migration_run(kv_store_if_t *kv,
                                                    const settings_migration_step_t *steps,
                                                    size_t steps_count)
{
    uint16_t version;
    if (!read_version(kv, &version)) {
        write_version(kv, SETTINGS_SCHEMA_VERSION_CURRENT);
        kv->commit(kv->ctx);
        return SETTINGS_MIGRATE_OK;
    }

    if (version > SETTINGS_SCHEMA_VERSION_CURRENT) {
        return SETTINGS_MIGRATE_ROLLBACK;
    }

    while (version < SETTINGS_SCHEMA_VERSION_CURRENT) {
        const settings_migration_step_t *step = find_step(steps, steps_count, version);
        if (step == NULL || !step->apply(kv)) {
            return SETTINGS_MIGRATE_STEP_FAILED;
        }

        version++;
        write_version(kv, version);
        kv->commit(kv->ctx);
    }

    return SETTINGS_MIGRATE_OK;
}
