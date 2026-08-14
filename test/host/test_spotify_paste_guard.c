/*
 * The pairing flow's three rejection paths, which docs/ROADMAP.md's Phase 6
 * exit note named as the ones never exercised. Each exists because
 * svc_spotify_submit_pasted_url() returns true in cases that then silently
 * do nothing, so these are the paths where a regression is invisible on
 * hardware -- the device reports a successful pairing that never happened.
 *
 * The happy paths are here too, because a guard that rejects everything
 * would satisfy the rejection tests alone.
 */

#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "spotify_paste_guard.h"

void setUp(void) {}
void tearDown(void) {}

/* --- rejection path 1: the pasted URL carries no usable code/state --- */

static void test_extracts_code_and_state_from_a_real_redirect(void)
{
    char v[64];

    TEST_ASSERT_TRUE(spotify_paste_extract_param(
        "http://127.0.0.1:8888/callback?code=AQD_abc123&state=nonce99", "code", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("AQD_abc123", v);

    TEST_ASSERT_TRUE(spotify_paste_extract_param(
        "http://127.0.0.1:8888/callback?code=AQD_abc123&state=nonce99", "state", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("nonce99", v);
}

static void test_missing_key_and_malformed_urls_are_rejected(void)
{
    char v[64];

    /* Spotify's own error redirect: a real URL, but no code in it. */
    TEST_ASSERT_FALSE(spotify_paste_extract_param(
        "http://127.0.0.1:8888/callback?error=access_denied&state=n1", "code", v, sizeof(v)));

    /* No query string at all -- what pasting the bare redirect URI gives. */
    TEST_ASSERT_FALSE(spotify_paste_extract_param("http://127.0.0.1:8888/callback", "code", v, sizeof(v)));

    /* Present as a bare key with no '=' must not be read as an empty value. */
    TEST_ASSERT_FALSE(spotify_paste_extract_param("http://x/cb?code", "code", v, sizeof(v)));

    TEST_ASSERT_FALSE(spotify_paste_extract_param(NULL, "code", v, sizeof(v)));
}

static void test_key_matches_only_a_whole_segment(void)
{
    char v[64];

    /* `barcode=` must not satisfy a request for `code`, and `state_x=` must
     * not satisfy `state` -- a prefix/suffix match here would let a crafted
     * URL smuggle a value past the nonce check. */
    TEST_ASSERT_FALSE(spotify_paste_extract_param("http://x/cb?barcode=nope", "code", v, sizeof(v)));
    TEST_ASSERT_FALSE(spotify_paste_extract_param("http://x/cb?state_x=nope", "state", v, sizeof(v)));

    /* The real key still wins when it follows a decoy. */
    TEST_ASSERT_TRUE(spotify_paste_extract_param("http://x/cb?barcode=nope&code=yes", "code", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("yes", v);
}

static void test_overlong_value_truncates_without_overrunning(void)
{
    /* Documented behaviour, not an accident: truncation is safe because a
     * truncated state then fails the nonce match below, and a truncated
     * code fails the exchange upstream. The guard byte proves the write
     * stayed inside the buffer. */
    char small[8];
    char guard[16];
    memset(guard, 0x5A, sizeof(guard));

    TEST_ASSERT_TRUE(spotify_paste_extract_param("http://x/cb?code=0123456789abcdef", "code", small,
                                                 sizeof(small)));
    TEST_ASSERT_EQUAL_size_t(sizeof(small) - 1, strlen(small));
    TEST_ASSERT_EQUAL_STRING("0123456", small);

    for (size_t i = 0; i < sizeof(guard); i++) {
        TEST_ASSERT_EQUAL_HEX8(0x5A, guard[i]);
    }
}

/* --- rejection path 2: the stale link (state nonce mismatch) --- */

static void test_matching_nonce_is_accepted(void)
{
    TEST_ASSERT_TRUE(spotify_paste_state_matches("nonce99", "nonce99"));
}

static void test_stale_link_from_an_earlier_attempt_is_rejected(void)
{
    /* The user pressed Authorize twice; the first tab's link still works in
     * their browser but its nonce is no longer the one in flight. */
    TEST_ASSERT_FALSE(spotify_paste_state_matches("nonce_second", "nonce_first"));
}

static void test_no_attempt_in_flight_never_matches(void)
{
    /* The replay window: with no pending nonce, accepting anything would
     * let a previously-captured redirect URL be replayed. Empty must lose
     * even against empty. */
    TEST_ASSERT_FALSE(spotify_paste_state_matches("", "anything"));
    TEST_ASSERT_FALSE(spotify_paste_state_matches("", ""));
    TEST_ASSERT_FALSE(spotify_paste_state_matches(NULL, "anything"));
    TEST_ASSERT_FALSE(spotify_paste_state_matches("nonce99", NULL));
}

static void test_nonce_match_is_exact_not_prefix(void)
{
    TEST_ASSERT_FALSE(spotify_paste_state_matches("nonce99", "nonce9"));
    TEST_ASSERT_FALSE(spotify_paste_state_matches("nonce99", "nonce99x"));
}

/* --- rejection path 3: the preconditions pair_http checks before submit --- */

static void test_precondition_ok_only_while_pairing_with_a_synced_clock(void)
{
    TEST_ASSERT_EQUAL(SPOTIFY_PASTE_PRECONDITION_OK,
                      spotify_paste_precondition(true, SPOTIFY_SESSION_PAIRING));
}

static void test_paste_before_sntp_sync_is_rejected(void)
{
    /* The token exchange needs a real clock to validate TLS certificates;
     * without one the CODE_PASTED command is dropped later with no error
     * the user ever sees. */
    TEST_ASSERT_EQUAL(SPOTIFY_PASTE_PRECONDITION_CLOCK_NOT_SYNCED,
                      spotify_paste_precondition(false, SPOTIFY_SESSION_PAIRING));
}

static void test_clock_is_reported_before_session_when_both_are_wrong(void)
{
    /* Order matters to the user: the clock resolves itself in seconds, so
     * it is the actionable one to report first. */
    TEST_ASSERT_EQUAL(SPOTIFY_PASTE_PRECONDITION_CLOCK_NOT_SYNCED,
                      spotify_paste_precondition(false, SPOTIFY_SESSION_UNPAIRED));
}

static void test_paste_outside_a_pairing_session_is_rejected(void)
{
    /* spotify_session_fsm ignores CODE_PASTED outside AWAITING_CODE with no
     * log at all, so every one of these must be caught here instead. */
    static const spotify_session_summary_t not_pairing[] = {
        SPOTIFY_SESSION_UNPAIRED,
        SPOTIFY_SESSION_ACTIVE,
        SPOTIFY_SESSION_REAUTH_REQUIRED,
        SPOTIFY_SESSION_REFRESHING,
    };

    for (size_t i = 0; i < sizeof(not_pairing) / sizeof(not_pairing[0]); i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "session_status=%d must not accept a paste", (int)not_pairing[i]);
        TEST_ASSERT_EQUAL_MESSAGE(SPOTIFY_PASTE_PRECONDITION_SESSION_NOT_AWAITING,
                                  spotify_paste_precondition(true, not_pairing[i]), msg);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_extracts_code_and_state_from_a_real_redirect);
    RUN_TEST(test_missing_key_and_malformed_urls_are_rejected);
    RUN_TEST(test_key_matches_only_a_whole_segment);
    RUN_TEST(test_overlong_value_truncates_without_overrunning);
    RUN_TEST(test_matching_nonce_is_accepted);
    RUN_TEST(test_stale_link_from_an_earlier_attempt_is_rejected);
    RUN_TEST(test_no_attempt_in_flight_never_matches);
    RUN_TEST(test_nonce_match_is_exact_not_prefix);
    RUN_TEST(test_precondition_ok_only_while_pairing_with_a_synced_clock);
    RUN_TEST(test_paste_before_sntp_sync_is_rejected);
    RUN_TEST(test_clock_is_reported_before_session_when_both_are_wrong);
    RUN_TEST(test_paste_outside_a_pairing_session_is_rejected);
    return UNITY_END();
}
