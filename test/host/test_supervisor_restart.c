/*
 * Host test for svc_supervisor_restart: attempt-count/window math for both
 * the sliding-window case (device reboot cap) and the lifetime-cap case
 * (per-service max_restarts), including expiry and the inclusive window
 * boundary.
 */

#include "unity.h"

#include "svc_supervisor_restart.h"

#include <stdint.h>

static restart_history_t g_h;

void setUp(void) { restart_history_init(&g_h); }
void tearDown(void) {}

static void test_fresh_history_allows_attempt(void)
{
    restart_decision_t d = restart_history_evaluate(&g_h, 1000, 1000000, 3, true);
    TEST_ASSERT_EQUAL(RESTART_DECISION_ATTEMPT, d);
}

static void test_hits_cap_degrades_when_degradable(void)
{
    TEST_ASSERT_EQUAL(RESTART_DECISION_ATTEMPT, restart_history_evaluate(&g_h, 0, 1000000, 2, true));
    TEST_ASSERT_EQUAL(RESTART_DECISION_ATTEMPT, restart_history_evaluate(&g_h, 100, 1000000, 2, true));
    TEST_ASSERT_EQUAL(RESTART_DECISION_DEGRADE, restart_history_evaluate(&g_h, 200, 1000000, 2, true));
}

static void test_hits_cap_fails_when_not_degradable(void)
{
    TEST_ASSERT_EQUAL(RESTART_DECISION_ATTEMPT, restart_history_evaluate(&g_h, 0, 1000000, 2, false));
    TEST_ASSERT_EQUAL(RESTART_DECISION_ATTEMPT, restart_history_evaluate(&g_h, 100, 1000000, 2, false));
    TEST_ASSERT_EQUAL(RESTART_DECISION_FAIL, restart_history_evaluate(&g_h, 200, 1000000, 2, false));
}

static void test_expired_attempts_free_up_budget(void)
{
    int64_t window = 1000000;   /* 1 s */
    TEST_ASSERT_EQUAL(RESTART_DECISION_ATTEMPT, restart_history_evaluate(&g_h, 0, window, 1, true));
    TEST_ASSERT_EQUAL(RESTART_DECISION_DEGRADE, restart_history_evaluate(&g_h, 500000, window, 1, true));
    /* Past the window now -- the first attempt has expired. */
    TEST_ASSERT_EQUAL(RESTART_DECISION_ATTEMPT, restart_history_evaluate(&g_h, 1000001, window, 1, true));
}

static void test_boundary_exactly_at_window_edge_still_counts(void)
{
    TEST_ASSERT_EQUAL(RESTART_DECISION_ATTEMPT, restart_history_evaluate(&g_h, 0, 1000, 1, true));
    /* now - ts == window exactly -- still within the window (inclusive). */
    TEST_ASSERT_EQUAL(RESTART_DECISION_DEGRADE, restart_history_evaluate(&g_h, 1000, 1000, 1, true));
    /* One tick past the edge -- now expired. */
    TEST_ASSERT_EQUAL(RESTART_DECISION_ATTEMPT, restart_history_evaluate(&g_h, 1001, 1000, 1, true));
}

static void test_lifetime_window_never_expires(void)
{
    TEST_ASSERT_EQUAL(RESTART_DECISION_ATTEMPT, restart_history_evaluate(&g_h, 0, INT64_MAX, 1, true));
    TEST_ASSERT_EQUAL(RESTART_DECISION_DEGRADE,
                       restart_history_evaluate(&g_h, (int64_t)999999999999LL, INT64_MAX, 1, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fresh_history_allows_attempt);
    RUN_TEST(test_hits_cap_degrades_when_degradable);
    RUN_TEST(test_hits_cap_fails_when_not_degradable);
    RUN_TEST(test_expired_attempts_free_up_budget);
    RUN_TEST(test_boundary_exactly_at_window_edge_still_counts);
    RUN_TEST(test_lifetime_window_never_expires);
    return UNITY_END();
}
