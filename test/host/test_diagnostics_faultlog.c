/*
 * Host test for svc_diagnostics_faultlog: rolling-last-8 order and
 * eviction on overflow.
 */

#include "unity.h"

#include "svc_diagnostics_faultlog.h"
#include "fake_kv_store.h"

#include <stdio.h>
#include <string.h>

static fake_kv_store_t g_backing;
static kv_store_if_t g_kv;

void setUp(void)
{
    fake_kv_store_init(&g_backing);
    g_kv = fake_kv_store_get(&g_backing);
}

void tearDown(void) {}

static fault_record_t make_record(uint32_t marker)
{
    fault_record_t r = { 0 };
    r.pc            = marker;
    r.reason         = FAULT_REASON_MANUAL;
    r.uptime_s       = marker * 10;
    r.heap_free_min = marker * 100;
    /* task_name doubles as a human-readable marker for assertions below */
    snprintf(r.task_name, FAULTLOG_TASK_NAME_MAX, "t%u", (unsigned)marker);
    return r;
}

static void test_load_on_empty_log_returns_zero(void)
{
    fault_record_t out[FAULTLOG_MAX_RECORDS];
    size_t n = faultlog_load(&g_kv, out, FAULTLOG_MAX_RECORDS);
    TEST_ASSERT_EQUAL(0, n);
}

static void test_append_then_load_returns_that_record(void)
{
    fault_record_t rec = make_record(1);
    TEST_ASSERT_TRUE(faultlog_append(&g_kv, &rec));

    fault_record_t out[FAULTLOG_MAX_RECORDS];
    size_t n = faultlog_load(&g_kv, out, FAULTLOG_MAX_RECORDS);

    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_UINT32(1, out[0].pc);
    TEST_ASSERT_EQUAL_STRING("t1", out[0].task_name);
}

static void test_multiple_appends_preserve_insertion_order(void)
{
    for (uint32_t i = 1; i <= 5; i++) {
        fault_record_t rec = make_record(i);
        TEST_ASSERT_TRUE(faultlog_append(&g_kv, &rec));
    }

    fault_record_t out[FAULTLOG_MAX_RECORDS];
    size_t n = faultlog_load(&g_kv, out, FAULTLOG_MAX_RECORDS);

    TEST_ASSERT_EQUAL(5, n);
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT32(i + 1, out[i].pc);
    }
}

static void test_filling_exactly_to_capacity_keeps_all_in_order(void)
{
    for (uint32_t i = 1; i <= FAULTLOG_MAX_RECORDS; i++) {
        fault_record_t rec = make_record(i);
        TEST_ASSERT_TRUE(faultlog_append(&g_kv, &rec));
    }

    fault_record_t out[FAULTLOG_MAX_RECORDS];
    size_t n = faultlog_load(&g_kv, out, FAULTLOG_MAX_RECORDS);

    TEST_ASSERT_EQUAL(FAULTLOG_MAX_RECORDS, n);
    for (size_t i = 0; i < FAULTLOG_MAX_RECORDS; i++) {
        TEST_ASSERT_EQUAL_UINT32(i + 1, out[i].pc);
    }
}

static void test_overflow_evicts_oldest_and_keeps_order(void)
{
    /* Fill to capacity (1..8), then push one more (9) -- record 1 must be
     * evicted, leaving 2..9 in order. */
    for (uint32_t i = 1; i <= FAULTLOG_MAX_RECORDS; i++) {
        fault_record_t rec = make_record(i);
        TEST_ASSERT_TRUE(faultlog_append(&g_kv, &rec));
    }
    fault_record_t overflow_rec = make_record(FAULTLOG_MAX_RECORDS + 1);
    TEST_ASSERT_TRUE(faultlog_append(&g_kv, &overflow_rec));

    fault_record_t out[FAULTLOG_MAX_RECORDS];
    size_t n = faultlog_load(&g_kv, out, FAULTLOG_MAX_RECORDS);

    TEST_ASSERT_EQUAL(FAULTLOG_MAX_RECORDS, n);
    for (size_t i = 0; i < FAULTLOG_MAX_RECORDS; i++) {
        TEST_ASSERT_EQUAL_UINT32(i + 2, out[i].pc);   /* 2..9 */
    }
}

static void test_repeated_overflow_keeps_sliding_window_correct(void)
{
    /* Push 20 records through an 8-deep log; the surviving window must be
     * exactly the last 8 (13..20), in order -- proves wraparound doesn't
     * corrupt ordering across multiple evictions, not just one. */
    for (uint32_t i = 1; i <= 20; i++) {
        fault_record_t rec = make_record(i);
        TEST_ASSERT_TRUE(faultlog_append(&g_kv, &rec));
    }

    fault_record_t out[FAULTLOG_MAX_RECORDS];
    size_t n = faultlog_load(&g_kv, out, FAULTLOG_MAX_RECORDS);

    TEST_ASSERT_EQUAL(FAULTLOG_MAX_RECORDS, n);
    for (size_t i = 0; i < FAULTLOG_MAX_RECORDS; i++) {
        TEST_ASSERT_EQUAL_UINT32(i + 13, out[i].pc);   /* 13..20 */
    }
}

static void test_load_respects_out_capacity_smaller_than_log(void)
{
    for (uint32_t i = 1; i <= FAULTLOG_MAX_RECORDS; i++) {
        fault_record_t rec = make_record(i);
        TEST_ASSERT_TRUE(faultlog_append(&g_kv, &rec));
    }

    fault_record_t out[3];
    size_t n = faultlog_load(&g_kv, out, 3);

    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_UINT32(1, out[0].pc);
    TEST_ASSERT_EQUAL_UINT32(2, out[1].pc);
    TEST_ASSERT_EQUAL_UINT32(3, out[2].pc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_on_empty_log_returns_zero);
    RUN_TEST(test_append_then_load_returns_that_record);
    RUN_TEST(test_multiple_appends_preserve_insertion_order);
    RUN_TEST(test_filling_exactly_to_capacity_keeps_all_in_order);
    RUN_TEST(test_overflow_evicts_oldest_and_keeps_order);
    RUN_TEST(test_repeated_overflow_keeps_sliding_window_correct);
    RUN_TEST(test_load_respects_out_capacity_smaller_than_log);
    return UNITY_END();
}
