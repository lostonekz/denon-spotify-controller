/*
 * Host test for svc_supervisor.c itself: boot-graph validation wired to
 * real status transitions, the device-level reboot cap actually calling
 * (and then withholding) the injected reboot_fn, and -- LIFECYCLE.md §1.2's
 * requirement that "each restartable service gets a host test that runs
 * create -> teardown -> create in a loop and asserts pool and handle counts
 * return to baseline" -- a fake restartable descriptor proving the
 * supervisor's own restart mechanism satisfies that pairing. No real
 * restartable service exists yet in Phase 1 (spotify/denon/artwork are all
 * later phases); this proves the mechanism ahead of a real registrant.
 */

#include "unity.h"

#include "svc_supervisor.h"
#include "fake_lock.h"
#include "fake_time_source.h"

#define N 3

static service_desc_t   g_descs[N];
static svc_state_t       g_states[N];
static svc_supervisor_t  g_sup;
static fake_time_source_t g_clock;

static int g_reboot_calls;

static void fake_reboot(void) { g_reboot_calls++; }

typedef struct {
    int live_count;       /* must return to baseline after every create->teardown->create */
    int create_calls;
    int teardown_calls;
} fake_resource_t;

static fake_resource_t g_resource;

static void fake_teardown(void *handle)
{
    (void)handle;
    g_resource.teardown_calls++;
    g_resource.live_count--;
}

static bool fake_restart(void *ctx, void **out_handle)
{
    fake_resource_t *res = (fake_resource_t *)ctx;
    res->create_calls++;
    res->live_count++;
    *out_handle = res;
    return true;
}

static bool failing_restart(void *ctx, void **out_handle)
{
    (void)ctx;
    (void)out_handle;
    return false;
}

void setUp(void)
{
    g_clock.now_us = 0;
    g_reboot_calls = 0;
    g_resource = (fake_resource_t){ 0 };

    g_descs[0] = (service_desc_t){
        .id = SVC_ID_STORAGE, .name = "storage", .requires = 0, .restart = RESTART_REBOOT,
    };
    g_descs[1] = (service_desc_t){
        .id = SVC_ID_SETTINGS, .name = "settings", .requires = SVC_BIT(SVC_ID_STORAGE),
        .restart = RESTART_IMMEDIATE, .max_restarts = 3, .degraded_ok = true,
        .teardown = fake_teardown, .restart_fn = fake_restart, .restart_ctx = &g_resource,
    };
    g_descs[2] = (service_desc_t){
        .id = SVC_ID_LOGGING, .name = "logging", .requires = SVC_BIT(SVC_ID_SETTINGS),
        .restart = RESTART_REBOOT,
    };

    svc_supervisor_init(&g_sup, g_descs, g_states, N, fake_lock_get(),
                          fake_time_source_get(&g_clock), fake_reboot);
}

void tearDown(void) {}

static void test_boot_marks_everything_ready_in_dependency_order(void)
{
    TEST_ASSERT_TRUE(svc_supervisor_boot(&g_sup));

    subsystem_status_t status;
    TEST_ASSERT_TRUE(svc_supervisor_get_status(&g_sup, SVC_ID_STORAGE, &status));
    TEST_ASSERT_EQUAL(SUBSYS_READY, status);
    TEST_ASSERT_TRUE(svc_supervisor_get_status(&g_sup, SVC_ID_SETTINGS, &status));
    TEST_ASSERT_EQUAL(SUBSYS_READY, status);
    TEST_ASSERT_TRUE(svc_supervisor_get_status(&g_sup, SVC_ID_LOGGING, &status));
    TEST_ASSERT_EQUAL(SUBSYS_READY, status);
}

static void test_boot_rejects_invalid_graph(void)
{
    /* Introduce a cycle: STORAGE -> LOGGING -> SETTINGS -> STORAGE. */
    g_descs[0].requires = SVC_BIT(SVC_ID_LOGGING);

    TEST_ASSERT_FALSE(svc_supervisor_boot(&g_sup));
}

static void test_get_status_false_for_unregistered_id(void)
{
    subsystem_status_t status;
    TEST_ASSERT_FALSE(svc_supervisor_get_status(&g_sup, SVC_ID_SPOTIFY, &status));
}

static void test_reboot_policy_failure_triggers_reboot(void)
{
    svc_supervisor_report_state(&g_sup, SVC_ID_STORAGE, SUBSYS_FAILED);
    TEST_ASSERT_EQUAL(1, g_reboot_calls);
}

static void test_reboot_cap_holds_after_three_attempts(void)
{
    g_clock.now_us = 0;
    svc_supervisor_report_state(&g_sup, SVC_ID_STORAGE, SUBSYS_FAILED);
    g_clock.now_us = 1000;
    svc_supervisor_report_state(&g_sup, SVC_ID_STORAGE, SUBSYS_FAILED);
    g_clock.now_us = 2000;
    svc_supervisor_report_state(&g_sup, SVC_ID_STORAGE, SUBSYS_FAILED);
    TEST_ASSERT_EQUAL(3, g_reboot_calls);

    g_clock.now_us = 3000;   /* still well within the 10-minute window */
    svc_supervisor_report_state(&g_sup, SVC_ID_STORAGE, SUBSYS_FAILED);
    TEST_ASSERT_EQUAL(3, g_reboot_calls);   /* held, not rebooted a 4th time */

    subsystem_status_t status;
    TEST_ASSERT_TRUE(svc_supervisor_get_status(&g_sup, SVC_ID_STORAGE, &status));
    TEST_ASSERT_EQUAL(SUBSYS_FAILED, status);
}

static void test_restartable_service_create_teardown_create_pairing(void)
{
    /* Isolate the pairing behavior from the restart cap (covered
     * separately by test_restart_cap_degrades_after_max_restarts) by
     * giving this loop more headroom than iterations. */
    g_descs[1].max_restarts = 100;
    g_resource.live_count = 1;   /* "created" as of setUp */

    for (int i = 0; i < 5; i++) {
        svc_supervisor_report_state(&g_sup, SVC_ID_SETTINGS, SUBSYS_FAILED);
        TEST_ASSERT_EQUAL(1, g_resource.live_count);   /* back to baseline every time */
    }

    TEST_ASSERT_EQUAL(5, g_resource.teardown_calls);
    TEST_ASSERT_EQUAL(5, g_resource.create_calls);

    subsystem_status_t status;
    TEST_ASSERT_TRUE(svc_supervisor_get_status(&g_sup, SVC_ID_SETTINGS, &status));
    TEST_ASSERT_EQUAL(SUBSYS_READY, status);
}

static void test_restart_fn_failure_marks_failed_even_when_degradable(void)
{
    g_descs[1].restart_fn = failing_restart;

    svc_supervisor_report_state(&g_sup, SVC_ID_SETTINGS, SUBSYS_FAILED);

    subsystem_status_t status;
    TEST_ASSERT_TRUE(svc_supervisor_get_status(&g_sup, SVC_ID_SETTINGS, &status));
    TEST_ASSERT_EQUAL(SUBSYS_FAILED, status);   /* the attempt itself failed -- not the cap */
}

static void test_restart_cap_degrades_after_max_restarts(void)
{
    for (int i = 0; i < 3; i++) {   /* max_restarts == 3 */
        svc_supervisor_report_state(&g_sup, SVC_ID_SETTINGS, SUBSYS_FAILED);
    }
    TEST_ASSERT_EQUAL(3, g_resource.create_calls);

    svc_supervisor_report_state(&g_sup, SVC_ID_SETTINGS, SUBSYS_FAILED);   /* 4th -- past the cap */

    subsystem_status_t status;
    TEST_ASSERT_TRUE(svc_supervisor_get_status(&g_sup, SVC_ID_SETTINGS, &status));
    TEST_ASSERT_EQUAL(SUBSYS_DEGRADED, status);
    TEST_ASSERT_EQUAL(3, g_resource.create_calls);   /* no further restart attempted */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_marks_everything_ready_in_dependency_order);
    RUN_TEST(test_boot_rejects_invalid_graph);
    RUN_TEST(test_get_status_false_for_unregistered_id);
    RUN_TEST(test_reboot_policy_failure_triggers_reboot);
    RUN_TEST(test_reboot_cap_holds_after_three_attempts);
    RUN_TEST(test_restartable_service_create_teardown_create_pairing);
    RUN_TEST(test_restart_fn_failure_marks_failed_even_when_degradable);
    RUN_TEST(test_restart_cap_degrades_after_max_restarts);
    return UNITY_END();
}
