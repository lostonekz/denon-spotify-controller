/*
 * Host test for the Denon command queue (coalescing + rate limit +
 * discrete-first interleaving). Runs on the dev machine. No ESP32.
 */

#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "denon_cmdq.h"

void setUp(void) {}
void tearDown(void) {}

static void test_empty_queue_nothing_due(void)
{
    denon_cmdq_t q;
    denon_cmdq_init(&q);

    denon_cmd_t out;
    TEST_ASSERT_FALSE(denon_cmdq_pop_due(&q, 0, &out));
}

static void test_first_absolute_send_is_immediate(void)
{
    denon_cmdq_t q;
    denon_cmdq_init(&q);

    denon_cmdq_submit_absolute(&q, DENON_CMD_CLASS_MV, "MV525");

    denon_cmd_t out;
    /* Never sent before -- must not wait out a rate-limit window that never started. */
    TEST_ASSERT_TRUE(denon_cmdq_pop_due(&q, 0, &out));
    TEST_ASSERT_EQUAL_STRING("MV525", out.cmd);

    /* Nothing else pending. */
    TEST_ASSERT_FALSE(denon_cmdq_pop_due(&q, 0, &out));
}

static void test_absolute_coalesces_to_latest(void)
{
    denon_cmdq_t q;
    denon_cmdq_init(&q);

    /* Simulates a volume drag: many submits before the queue is ever drained. */
    denon_cmdq_submit_absolute(&q, DENON_CMD_CLASS_MV, "MV500");
    denon_cmdq_submit_absolute(&q, DENON_CMD_CLASS_MV, "MV505");
    denon_cmdq_submit_absolute(&q, DENON_CMD_CLASS_MV, "MV510");
    denon_cmdq_submit_absolute(&q, DENON_CMD_CLASS_MV, "MV515");

    denon_cmd_t out;
    TEST_ASSERT_TRUE(denon_cmdq_pop_due(&q, 0, &out));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("MV515", out.cmd, "only the newest value must survive coalescing");

    /* The three superseded values are gone, not queued behind the winner. */
    TEST_ASSERT_FALSE(denon_cmdq_pop_due(&q, 0, &out));
}

static void test_absolute_rate_limited_after_first_send(void)
{
    denon_cmdq_t q;
    denon_cmdq_init(&q);

    denon_cmdq_submit_absolute(&q, DENON_CMD_CLASS_MV, "MV500");
    denon_cmd_t out;
    TEST_ASSERT_TRUE(denon_cmdq_pop_due(&q, 1000, &out));   /* sent at t=1000us */

    /* A new value arrives immediately after -- still inside the 100ms window. */
    denon_cmdq_submit_absolute(&q, DENON_CMD_CLASS_MV, "MV505");
    TEST_ASSERT_FALSE_MESSAGE(denon_cmdq_pop_due(&q, 1000 + 99999, &out),
                                "99.999ms after the last send must still be rate-limited");

    /* Exactly at the 100ms boundary it is due. */
    TEST_ASSERT_TRUE(denon_cmdq_pop_due(&q, 1000 + DENON_CMDQ_ABSOLUTE_RATE_LIMIT_US, &out));
    TEST_ASSERT_EQUAL_STRING("MV505", out.cmd);
}

static void test_discrete_fifo_order_never_coalesced(void)
{
    denon_cmdq_t q;
    denon_cmdq_init(&q);

    TEST_ASSERT_TRUE(denon_cmdq_submit_discrete(&q, "PWON"));
    TEST_ASSERT_TRUE(denon_cmdq_submit_discrete(&q, "MUON"));
    TEST_ASSERT_TRUE(denon_cmdq_submit_discrete(&q, "MUOFF"));

    denon_cmd_t out;
    /* Same rate-limit tick each time -- discretes are never throttled. */
    TEST_ASSERT_TRUE(denon_cmdq_pop_due(&q, 0, &out));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("PWON", out.cmd, "discrete FIFO order");
    TEST_ASSERT_TRUE(denon_cmdq_pop_due(&q, 0, &out));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("MUON", out.cmd, "MUON must not be coalesced away by MUOFF");
    TEST_ASSERT_TRUE(denon_cmdq_pop_due(&q, 0, &out));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("MUOFF", out.cmd, "discrete FIFO order");

    TEST_ASSERT_FALSE(denon_cmdq_pop_due(&q, 0, &out));
}

static void test_discrete_drains_before_absolute(void)
{
    denon_cmdq_t q;
    denon_cmdq_init(&q);

    denon_cmdq_submit_absolute(&q, DENON_CMD_CLASS_MV, "MV525");
    denon_cmdq_submit_discrete(&q, "PWON");

    denon_cmd_t out;
    TEST_ASSERT_TRUE(denon_cmdq_pop_due(&q, 0, &out));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("PWON", out.cmd,
        "a power/mute/input tap must never wait behind a volume drag's rate limit");

    TEST_ASSERT_TRUE(denon_cmdq_pop_due(&q, 0, &out));
    TEST_ASSERT_EQUAL_STRING("MV525", out.cmd);
}

static void test_discrete_fifo_full_drops_newest_not_oldest(void)
{
    denon_cmdq_t q;
    denon_cmdq_init(&q);

    char cmd[8];
    for (size_t i = 0; i < DENON_CMDQ_DISCRETE_DEPTH; i++) {
        snprintf(cmd, sizeof(cmd), "C%zu", i);
        TEST_ASSERT_TRUE(denon_cmdq_submit_discrete(&q, cmd));
    }
    /* Queue is now full -- the next submit must fail, not evict "C0". */
    TEST_ASSERT_FALSE(denon_cmdq_submit_discrete(&q, "OVERFLOW"));

    denon_cmd_t out;
    TEST_ASSERT_TRUE(denon_cmdq_pop_due(&q, 0, &out));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("C0", out.cmd, "the oldest entry must survive an overflow attempt");
}

static void test_submit_ignores_null(void)
{
    denon_cmdq_t q;
    denon_cmdq_init(&q);

    denon_cmdq_submit_absolute(&q, DENON_CMD_CLASS_MV, NULL);
    TEST_ASSERT_FALSE(denon_cmdq_submit_discrete(&q, NULL));

    denon_cmd_t out;
    TEST_ASSERT_FALSE(denon_cmdq_pop_due(&q, 0, &out));
}

/* ------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_queue_nothing_due);
    RUN_TEST(test_first_absolute_send_is_immediate);
    RUN_TEST(test_absolute_coalesces_to_latest);
    RUN_TEST(test_absolute_rate_limited_after_first_send);
    RUN_TEST(test_discrete_fifo_order_never_coalesced);
    RUN_TEST(test_discrete_drains_before_absolute);
    RUN_TEST(test_discrete_fifo_full_drops_newest_not_oldest);
    RUN_TEST(test_submit_ignores_null);
    return UNITY_END();
}
