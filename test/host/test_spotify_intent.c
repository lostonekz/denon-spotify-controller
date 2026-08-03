/*
 * Host test for spotify_intent: every playback_intent_kind_t translates to
 * the right spotify_api_cmd_t, wrong-domain/unknown-kind are rejected, and
 * volume_pct is clamped to 0-100 at this protocol boundary.
 */

#include "unity.h"

#include "spotify_intent.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static intent_t make_intent(intent_domain_t domain, uint16_t kind, const void *payload, size_t payload_len)
{
    intent_t intent = { 0 };
    intent.domain = domain;
    intent.kind   = kind;
    /* memcpy(dst, NULL, 0) is technically UB per the C standard even at
     * length 0 -- UBSan's nonnull-attribute check fires on the NULL
     * argument alone, regardless of length (real finding: this test file
     * was passing (NULL, 0) at 7 of its own 11 call sites, silently never
     * caught before because the ASan/UBSan CI job never actually reached
     * this Test step -- see test/host/CMakeLists.txt's own fix history). */
    if (payload != NULL && payload_len > 0) {
        memcpy(intent.payload, payload, payload_len);
    }
    return intent;
}

static void test_play(void)
{
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_PLAY, NULL, 0);
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_OK, spotify_intent_to_cmd(&intent, &cmd));
    TEST_ASSERT_EQUAL(SPOTIFY_API_CMD_PLAY, cmd.kind);
}

static void test_pause(void)
{
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_PAUSE, NULL, 0);
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_OK, spotify_intent_to_cmd(&intent, &cmd));
    TEST_ASSERT_EQUAL(SPOTIFY_API_CMD_PAUSE, cmd.kind);
}

static void test_next(void)
{
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_NEXT, NULL, 0);
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_OK, spotify_intent_to_cmd(&intent, &cmd));
    TEST_ASSERT_EQUAL(SPOTIFY_API_CMD_NEXT, cmd.kind);
}

static void test_previous(void)
{
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_PREVIOUS, NULL, 0);
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_OK, spotify_intent_to_cmd(&intent, &cmd));
    TEST_ASSERT_EQUAL(SPOTIFY_API_CMD_PREVIOUS, cmd.kind);
}

static void test_set_shuffle(void)
{
    playback_intent_payload_t p = { .shuffle = true };
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_SET_SHUFFLE, &p, sizeof(p));
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_OK, spotify_intent_to_cmd(&intent, &cmd));
    TEST_ASSERT_EQUAL(SPOTIFY_API_CMD_SET_SHUFFLE, cmd.kind);
    TEST_ASSERT_TRUE(cmd.u.shuffle);
}

static void test_set_repeat(void)
{
    playback_intent_payload_t p = { .repeat_mode = 2 };
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_SET_REPEAT, &p, sizeof(p));
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_OK, spotify_intent_to_cmd(&intent, &cmd));
    TEST_ASSERT_EQUAL(SPOTIFY_API_CMD_SET_REPEAT, cmd.kind);
    TEST_ASSERT_EQUAL(2, cmd.u.repeat_mode);
}

static void test_set_volume_in_range(void)
{
    playback_intent_payload_t p = { .volume_pct = 42 };
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_SET_VOLUME, &p, sizeof(p));
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_OK, spotify_intent_to_cmd(&intent, &cmd));
    TEST_ASSERT_EQUAL(SPOTIFY_API_CMD_SET_VOLUME, cmd.kind);
    TEST_ASSERT_EQUAL(42, cmd.u.volume_pct);
}

static void test_set_volume_above_100_is_clamped(void)
{
    playback_intent_payload_t p = { .volume_pct = 255 };
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_SET_VOLUME, &p, sizeof(p));
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_OK, spotify_intent_to_cmd(&intent, &cmd));
    TEST_ASSERT_EQUAL(100, cmd.u.volume_pct);
}

static void test_seek(void)
{
    playback_intent_payload_t p = { .seek_position_ms = 63500 };
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_SEEK, &p, sizeof(p));
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_OK, spotify_intent_to_cmd(&intent, &cmd));
    TEST_ASSERT_EQUAL(SPOTIFY_API_CMD_SEEK, cmd.kind);
    TEST_ASSERT_EQUAL_UINT32(63500, cmd.u.position_ms);
}

static void test_wrong_domain_rejected(void)
{
    intent_t intent = make_intent(INTENT_DOMAIN_DENON, PLAYBACK_INTENT_PLAY, NULL, 0);
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_WRONG_DOMAIN, spotify_intent_to_cmd(&intent, &cmd));
}

static void test_unknown_kind_rejected(void)
{
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, 999, NULL, 0);
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_UNKNOWN_KIND, spotify_intent_to_cmd(&intent, &cmd));
}

static void test_resume_on_avr_not_handled_here(void)
{
    /* PLAYBACK_INTENT_RESUME_ON_AVR is real but deliberately NOT in this
     * translator's switch -- it needs svc_spotify_t's own persisted AVR
     * device-id pin, which this pure function has no access to, so
     * spotify_service_task.c's handle_playback_intent() intercepts it
     * before ever calling here (see playback_intent_types.h's own comment
     * on this kind). This pins that contract: if a caller reaches this
     * function with it anyway, the answer is still a clean rejection, not
     * a crash or a silently wrong command. */
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_RESUME_ON_AVR, NULL, 0);
    spotify_api_cmd_t cmd;
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_UNKNOWN_KIND, spotify_intent_to_cmd(&intent, &cmd));
}

static void test_null_args_rejected(void)
{
    intent_t intent = make_intent(INTENT_DOMAIN_PLAYBACK, PLAYBACK_INTENT_PLAY, NULL, 0);
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_UNKNOWN_KIND, spotify_intent_to_cmd(NULL, NULL));
    TEST_ASSERT_EQUAL(SPOTIFY_INTENT_RESULT_UNKNOWN_KIND, spotify_intent_to_cmd(&intent, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_play);
    RUN_TEST(test_pause);
    RUN_TEST(test_next);
    RUN_TEST(test_previous);
    RUN_TEST(test_set_shuffle);
    RUN_TEST(test_set_repeat);
    RUN_TEST(test_set_volume_in_range);
    RUN_TEST(test_set_volume_above_100_is_clamped);
    RUN_TEST(test_seek);
    RUN_TEST(test_wrong_domain_rejected);
    RUN_TEST(test_unknown_kind_rejected);
    RUN_TEST(test_resume_on_avr_not_handled_here);
    RUN_TEST(test_null_args_rejected);
    return UNITY_END();
}
