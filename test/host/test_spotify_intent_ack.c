/*
 * Host test for spotify_intent_ack.c -- Phase 5 Tier -1's extracted pure
 * "which ack kind" decision (docs/EVENTS_AND_STATE.md §6.3). The actual bus
 * publish and the queue-drain-while-unauthorized control flow live in
 * target-only spotify_service_task.c, verified by idf.py build + Tier 7
 * hardware instead -- see spotify_service.h's own top comment.
 */
#include "unity.h"

#include "spotify_intent_ack.h"

void setUp(void) {}
void tearDown(void) {}

static void test_ok_is_accepted(void)
{
    TEST_ASSERT_EQUAL(INTENT_ACK_EVT_ACCEPTED,
                        spotify_intent_ack_kind_for_result(SPOTIFY_API_RESULT_OK));
}

static void test_no_active_device_is_rejected(void)
{
    /* Not an error for a plain status poll, but a real command failure from
     * the intent's own point of view -- there was nothing to play/pause/skip. */
    TEST_ASSERT_EQUAL(INTENT_ACK_EVT_REJECTED,
                        spotify_intent_ack_kind_for_result(SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE));
}

static void test_unauthorized_is_rejected(void)
{
    TEST_ASSERT_EQUAL(INTENT_ACK_EVT_REJECTED,
                        spotify_intent_ack_kind_for_result(SPOTIFY_API_RESULT_UNAUTHORIZED));
}

static void test_failed_is_rejected(void)
{
    TEST_ASSERT_EQUAL(INTENT_ACK_EVT_REJECTED,
                        spotify_intent_ack_kind_for_result(SPOTIFY_API_RESULT_FAILED));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ok_is_accepted);
    RUN_TEST(test_no_active_device_is_rejected);
    RUN_TEST(test_unauthorized_is_rejected);
    RUN_TEST(test_failed_is_rejected);
    return UNITY_END();
}
