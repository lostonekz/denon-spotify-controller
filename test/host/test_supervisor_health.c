/*
 * Host test for svc_supervisor_health: PASS the instant every condition is
 * true (regardless of report order), TIMED_OUT at the 120s boundary, and
 * conditions are sticky (once true, stay true).
 */

#include "unity.h"

#include "svc_supervisor_health.h"

static svc_supervisor_health_t g_gate;

void setUp(void)
{
    svc_supervisor_health_init(&g_gate, 0);
}

void tearDown(void) {}

static void report_all(bool ok)
{
    for (int i = 0; i < HEALTH_COND_COUNT; i++) {
        svc_supervisor_health_report(&g_gate, (health_condition_t)i, ok);
    }
}

static void test_fresh_gate_is_pending(void)
{
    TEST_ASSERT_EQUAL(HEALTH_GATE_PENDING, svc_supervisor_health_evaluate(&g_gate, 0));
}

static void test_all_conditions_true_passes_immediately(void)
{
    report_all(true);
    TEST_ASSERT_EQUAL(HEALTH_GATE_PASS, svc_supervisor_health_evaluate(&g_gate, 0));
}

static void test_missing_one_condition_stays_pending_within_window(void)
{
    for (int i = 0; i < HEALTH_COND_COUNT; i++) {
        if ((health_condition_t)i != HEALTH_COND_WIFI_IP) {
            svc_supervisor_health_report(&g_gate, (health_condition_t)i, true);
        }
    }

    TEST_ASSERT_EQUAL(HEALTH_GATE_PENDING,
                        svc_supervisor_health_evaluate(&g_gate, HEALTH_GATE_WINDOW_US - 1));
}

static void test_incomplete_conditions_time_out_at_the_boundary(void)
{
    svc_supervisor_health_report(&g_gate, HEALTH_COND_BOOT_COMPLETE, true);   /* deliberately incomplete */

    TEST_ASSERT_EQUAL(HEALTH_GATE_PENDING,
                        svc_supervisor_health_evaluate(&g_gate, HEALTH_GATE_WINDOW_US - 1));
    TEST_ASSERT_EQUAL(HEALTH_GATE_TIMED_OUT,
                        svc_supervisor_health_evaluate(&g_gate, HEALTH_GATE_WINDOW_US));
}

static void test_complete_conditions_pass_even_past_the_window(void)
{
    /* PASS is checked before the timeout -- finishing late still counts,
     * since the gate polls at its own cadence and isn't guaranteed to
     * observe the exact instant every condition became true. */
    report_all(true);
    TEST_ASSERT_EQUAL(HEALTH_GATE_PASS,
                        svc_supervisor_health_evaluate(&g_gate, HEALTH_GATE_WINDOW_US + 1000));
}

static void test_conditions_are_sticky_once_true(void)
{
    svc_supervisor_health_report(&g_gate, HEALTH_COND_WIFI_IP, true);
    svc_supervisor_health_report(&g_gate, HEALTH_COND_WIFI_IP, false);   /* must not unset it */

    report_all(true);   /* fill in the rest */
    svc_supervisor_health_report(&g_gate, HEALTH_COND_WIFI_IP, false);   /* still must not unset it */

    TEST_ASSERT_EQUAL(HEALTH_GATE_PASS, svc_supervisor_health_evaluate(&g_gate, 0));
}

static void test_report_order_does_not_matter(void)
{
    /* Report every condition in reverse order -- same end result. */
    for (int i = HEALTH_COND_COUNT - 1; i >= 0; i--) {
        svc_supervisor_health_report(&g_gate, (health_condition_t)i, true);
    }
    TEST_ASSERT_EQUAL(HEALTH_GATE_PASS, svc_supervisor_health_evaluate(&g_gate, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fresh_gate_is_pending);
    RUN_TEST(test_all_conditions_true_passes_immediately);
    RUN_TEST(test_missing_one_condition_stays_pending_within_window);
    RUN_TEST(test_incomplete_conditions_time_out_at_the_boundary);
    RUN_TEST(test_complete_conditions_pass_even_past_the_window);
    RUN_TEST(test_conditions_are_sticky_once_true);
    RUN_TEST(test_report_order_does_not_matter);
    return UNITY_END();
}
