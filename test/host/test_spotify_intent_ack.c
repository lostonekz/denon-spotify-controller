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

static void test_no_active_device_is_its_own_kind_not_rejected(void)
{
    /* Deliberately NOT INTENT_ACK_EVT_REJECTED, which this used to assert.
     * The command did fail from the intent's point of view -- there was
     * nothing to play/pause/skip -- but the cause is categorically different
     * from a failed or unreachable call, and real hardware proved the
     * distinction matters: an AVR whose Spotify Connect session flaps in and
     * out made this a routine occurrence, and collapsing it into REJECTED
     * forced the UI to claim "Couldn't reach Spotify" about a request
     * Spotify had answered correctly (HTTP 404, reason NO_ACTIVE_DEVICE).
     * A distinct kind is what lets the UI say something true, and lets
     * svc_spotify suppress a retry storm it can answer locally. */
    TEST_ASSERT_EQUAL(INTENT_ACK_EVT_NO_ACTIVE_DEVICE,
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
    RUN_TEST(test_no_active_device_is_its_own_kind_not_rejected);
    RUN_TEST(test_unauthorized_is_rejected);
    RUN_TEST(test_failed_is_rejected);
    return UNITY_END();
}
