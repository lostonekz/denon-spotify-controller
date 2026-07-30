/*
 * Host test for svc_settings_migration: fresh device defaults to current
 * version with no steps, older stored versions walk the matching step
 * chain, a newer stored version is treated as a rollback, and a missing or
 * failing step aborts without partial progress.
 */

#include "unity.h"

#include "svc_settings_migration.h"
#include "fake_kv_store.h"

static fake_kv_store_t g_backing;
static kv_store_if_t g_kv;

static int g_step_calls;

void setUp(void)
{
    fake_kv_store_init(&g_backing);
    g_kv = fake_kv_store_get(&g_backing);
    g_step_calls = 0;
}

void tearDown(void) {}

static uint16_t stored_version(void)
{
    uint16_t v = 0xFFFF;
    size_t len = sizeof(v);
    TEST_ASSERT_TRUE(g_kv.get_blob(g_kv.ctx, SETTINGS_SCHEMA_VERSION_KEY, &v, &len));
    return v;
}

static void seed_version(uint16_t v)
{
    TEST_ASSERT_TRUE(g_kv.set_blob(g_kv.ctx, SETTINGS_SCHEMA_VERSION_KEY, &v, sizeof(v)));
}

static bool step_0_to_1(kv_store_if_t *kv)
{
    (void)kv;
    g_step_calls++;
    return true;
}

static bool failing_step(kv_store_if_t *kv)
{
    (void)kv;
    g_step_calls++;
    return false;
}

static void test_fresh_device_initializes_to_current_version_with_no_steps(void)
{
    settings_migrate_result_t r = settings_migration_run(&g_kv, NULL, 0);

    TEST_ASSERT_EQUAL(SETTINGS_MIGRATE_OK, r);
    TEST_ASSERT_EQUAL(0, g_step_calls);
    TEST_ASSERT_EQUAL_UINT16(SETTINGS_SCHEMA_VERSION_CURRENT, stored_version());
}

static void test_already_current_version_runs_no_steps(void)
{
    seed_version(SETTINGS_SCHEMA_VERSION_CURRENT);

    settings_migrate_result_t r = settings_migration_run(&g_kv, NULL, 0);

    TEST_ASSERT_EQUAL(SETTINGS_MIGRATE_OK, r);
    TEST_ASSERT_EQUAL(0, g_step_calls);
    TEST_ASSERT_EQUAL_UINT16(SETTINGS_SCHEMA_VERSION_CURRENT, stored_version());
}

static void test_older_version_runs_matching_step_and_reaches_current(void)
{
    seed_version(0);
    settings_migration_step_t steps[] = { { .from_version = 0, .apply = step_0_to_1 } };

    settings_migrate_result_t r = settings_migration_run(&g_kv, steps, 1);

    TEST_ASSERT_EQUAL(SETTINGS_MIGRATE_OK, r);
    TEST_ASSERT_EQUAL(1, g_step_calls);
    TEST_ASSERT_EQUAL_UINT16(SETTINGS_SCHEMA_VERSION_CURRENT, stored_version());
}

static void test_newer_stored_version_is_rollback_and_nothing_is_modified(void)
{
    uint16_t newer = (uint16_t)(SETTINGS_SCHEMA_VERSION_CURRENT + 1);
    seed_version(newer);

    settings_migrate_result_t r = settings_migration_run(&g_kv, NULL, 0);

    TEST_ASSERT_EQUAL(SETTINGS_MIGRATE_ROLLBACK, r);
    TEST_ASSERT_EQUAL(0, g_step_calls);
    TEST_ASSERT_EQUAL_UINT16(newer, stored_version());   /* untouched */
}

static void test_missing_step_definition_fails_without_partial_progress(void)
{
    seed_version(0);

    settings_migrate_result_t r = settings_migration_run(&g_kv, NULL, 0);   /* no steps at all */

    TEST_ASSERT_EQUAL(SETTINGS_MIGRATE_STEP_FAILED, r);
    TEST_ASSERT_EQUAL_UINT16(0, stored_version());   /* unchanged -- retried next boot */
}

static void test_failing_step_aborts_without_bumping_version(void)
{
    seed_version(0);
    settings_migration_step_t steps[] = { { .from_version = 0, .apply = failing_step } };

    settings_migrate_result_t r = settings_migration_run(&g_kv, steps, 1);

    TEST_ASSERT_EQUAL(SETTINGS_MIGRATE_STEP_FAILED, r);
    TEST_ASSERT_EQUAL(1, g_step_calls);
    TEST_ASSERT_EQUAL_UINT16(0, stored_version());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fresh_device_initializes_to_current_version_with_no_steps);
    RUN_TEST(test_already_current_version_runs_no_steps);
    RUN_TEST(test_older_version_runs_matching_step_and_reaches_current);
    RUN_TEST(test_newer_stored_version_is_rollback_and_nothing_is_modified);
    RUN_TEST(test_missing_step_definition_fails_without_partial_progress);
    RUN_TEST(test_failing_step_aborts_without_bumping_version);
    return UNITY_END();
}
