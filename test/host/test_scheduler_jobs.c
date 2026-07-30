/*
 * Host test for svc_scheduler_jobs: due-time ordering under a fake clock,
 * one-shot vs periodic rescheduling, catch-up snapping, and stale-handle
 * rejection on cancel.
 */

#include "unity.h"

#include "svc_scheduler_jobs.h"

#define CAP 4

static scheduler_job_t g_storage[CAP];
static scheduler_job_table_t g_table;

static int g_calls;
static void *g_last_ctx;

static void record_call(void *ctx)
{
    g_calls++;
    g_last_ctx = ctx;
}

void setUp(void)
{
    g_calls = 0;
    g_last_ctx = NULL;
    scheduler_job_table_init(&g_table, g_storage, CAP);
}

void tearDown(void) {}

static void test_pop_due_false_before_due_time(void)
{
    scheduler_job_h h;
    TEST_ASSERT_TRUE(scheduler_job_table_post(&g_table, 0, 1000, 0, record_call, NULL, &h));

    scheduler_job_fn fn;
    void *ctx;
    TEST_ASSERT_FALSE(scheduler_job_table_pop_due(&g_table, 500, &fn, &ctx));
}

static void test_pop_due_fires_at_due_time_then_frees_slot(void)
{
    scheduler_job_h h;
    int marker = 42;
    TEST_ASSERT_TRUE(scheduler_job_table_post(&g_table, 0, 1000, 0, record_call, &marker, &h));

    scheduler_job_fn fn;
    void *ctx;
    TEST_ASSERT_TRUE(scheduler_job_table_pop_due(&g_table, 1000, &fn, &ctx));
    fn(ctx);
    TEST_ASSERT_EQUAL(1, g_calls);
    TEST_ASSERT_EQUAL_PTR(&marker, g_last_ctx);

    /* One-shot: slot freed, nothing left due. */
    TEST_ASSERT_FALSE(scheduler_job_table_pop_due(&g_table, 1000, &fn, &ctx));
    TEST_ASSERT_EQUAL_INT64(INT64_MAX, scheduler_job_table_next_due(&g_table));
}

static void test_pop_due_returns_earliest_first(void)
{
    scheduler_job_h h1, h2;
    int a = 1, b = 2;
    TEST_ASSERT_TRUE(scheduler_job_table_post(&g_table, 0, 500, 0, record_call, &a, &h1));
    TEST_ASSERT_TRUE(scheduler_job_table_post(&g_table, 0, 100, 0, record_call, &b, &h2));

    scheduler_job_fn fn;
    void *ctx;
    TEST_ASSERT_TRUE(scheduler_job_table_pop_due(&g_table, 1000, &fn, &ctx));
    TEST_ASSERT_EQUAL_PTR(&b, ctx);    /* due at 100, earlier */

    TEST_ASSERT_TRUE(scheduler_job_table_pop_due(&g_table, 1000, &fn, &ctx));
    TEST_ASSERT_EQUAL_PTR(&a, ctx);    /* due at 500 */

    TEST_ASSERT_FALSE(scheduler_job_table_pop_due(&g_table, 1000, &fn, &ctx));
}

static void test_periodic_job_reschedules_and_stays_active(void)
{
    scheduler_job_h h;
    TEST_ASSERT_TRUE(scheduler_job_table_post(&g_table, 0, 100, 100, record_call, NULL, &h));

    scheduler_job_fn fn;
    void *ctx;
    TEST_ASSERT_TRUE(scheduler_job_table_pop_due(&g_table, 100, &fn, &ctx));
    TEST_ASSERT_EQUAL_INT64(200, scheduler_job_table_next_due(&g_table));

    TEST_ASSERT_FALSE(scheduler_job_table_pop_due(&g_table, 150, &fn, &ctx));   /* not due yet */
    TEST_ASSERT_TRUE(scheduler_job_table_pop_due(&g_table, 200, &fn, &ctx));
    TEST_ASSERT_EQUAL_INT64(300, scheduler_job_table_next_due(&g_table));
}

static void test_periodic_job_falling_behind_snaps_forward_instead_of_bursting(void)
{
    scheduler_job_h h;
    TEST_ASSERT_TRUE(scheduler_job_table_post(&g_table, 0, 100, 100, record_call, NULL, &h));

    /* Worker didn't run for a while; "now" is far past several periods. */
    scheduler_job_fn fn;
    void *ctx;
    TEST_ASSERT_TRUE(scheduler_job_table_pop_due(&g_table, 10000, &fn, &ctx));
    fn(ctx);
    TEST_ASSERT_EQUAL(1, g_calls);                                             /* fired once, not 99 times */
    TEST_ASSERT_EQUAL_INT64(10100, scheduler_job_table_next_due(&g_table));    /* snapped to now + period */

    TEST_ASSERT_FALSE(scheduler_job_table_pop_due(&g_table, 10000, &fn, &ctx));
}

static void test_cancel_removes_pending_job(void)
{
    scheduler_job_h h;
    TEST_ASSERT_TRUE(scheduler_job_table_post(&g_table, 0, 100, 0, record_call, NULL, &h));

    scheduler_job_table_cancel(&g_table, h);

    scheduler_job_fn fn;
    void *ctx;
    TEST_ASSERT_FALSE(scheduler_job_table_pop_due(&g_table, 100, &fn, &ctx));
    TEST_ASSERT_EQUAL_INT64(INT64_MAX, scheduler_job_table_next_due(&g_table));
}

static void test_cancel_is_noop_on_stale_handle_after_slot_reuse(void)
{
    scheduler_job_h first, second;
    int a = 1, b = 2;
    TEST_ASSERT_TRUE(scheduler_job_table_post(&g_table, 0, 100, 0, record_call, &a, &first));

    scheduler_job_fn fn;
    void *ctx;
    TEST_ASSERT_TRUE(scheduler_job_table_pop_due(&g_table, 100, &fn, &ctx));   /* fires, frees slot */

    TEST_ASSERT_TRUE(scheduler_job_table_post(&g_table, 100, 100, 0, record_call, &b, &second));
    TEST_ASSERT_EQUAL_UINT32(first.index, second.index);          /* slot reused */
    TEST_ASSERT_NOT_EQUAL(first.generation, second.generation);

    scheduler_job_table_cancel(&g_table, first);   /* stale -- must not cancel `second` */

    TEST_ASSERT_TRUE(scheduler_job_table_pop_due(&g_table, 200, &fn, &ctx));
    TEST_ASSERT_EQUAL_PTR(&b, ctx);
}

static void test_post_fails_when_table_full(void)
{
    scheduler_job_h h;
    for (int i = 0; i < CAP; i++) {
        TEST_ASSERT_TRUE(scheduler_job_table_post(&g_table, 0, 100, 0, record_call, NULL, &h));
    }
    TEST_ASSERT_FALSE(scheduler_job_table_post(&g_table, 0, 100, 0, record_call, NULL, &h));
}

static void test_next_due_is_max_when_empty(void)
{
    TEST_ASSERT_EQUAL_INT64(INT64_MAX, scheduler_job_table_next_due(&g_table));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pop_due_false_before_due_time);
    RUN_TEST(test_pop_due_fires_at_due_time_then_frees_slot);
    RUN_TEST(test_pop_due_returns_earliest_first);
    RUN_TEST(test_periodic_job_reschedules_and_stays_active);
    RUN_TEST(test_periodic_job_falling_behind_snaps_forward_instead_of_bursting);
    RUN_TEST(test_cancel_removes_pending_job);
    RUN_TEST(test_cancel_is_noop_on_stale_handle_after_slot_reuse);
    RUN_TEST(test_post_fails_when_table_full);
    RUN_TEST(test_next_due_is_max_when_empty);
    return UNITY_END();
}
