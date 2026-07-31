/*
 * Restartability test for svc_spotify's pure state (spotify_service_state.h).
 * LIFECYCLE.md §1.2: a restartable service needs a host test that runs
 * create->teardown->create in a loop and asserts state returns to
 * baseline. See spotify_service_state.h's top comment for why THIS is the
 * piece that can be host-tested, not svc_spotify_t itself.
 */

#include <string.h>

#include "unity.h"

#include "spotify_service_state.h"

void setUp(void) {}
void tearDown(void) {}

static spotify_service_state_cfg_t default_cfg(void)
{
    spotify_service_state_cfg_t cfg = { 0 };
    strncpy(cfg.client_id, "abc123", sizeof(cfg.client_id) - 1);
    strncpy(cfg.redirect_uri, "http://127.0.0.1:8888/callback", sizeof(cfg.redirect_uri) - 1);
    return cfg;
}

static void test_init_with_no_stored_creds_starts_unpaired(void)
{
    spotify_service_state_t s;
    spotify_service_state_cfg_t cfg = default_cfg();

    TEST_ASSERT_TRUE(spotify_service_state_init(&s, &cfg));
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_UNPAIRED, spotify_session_fsm_current_state(&s.session_fsm));
    TEST_ASSERT_FALSE(s.resume_pending);
    TEST_ASSERT_EQUAL_STRING("abc123", s.client_id);
}

static void test_init_with_stored_creds_starts_resuming_with_pending_flag(void)
{
    spotify_service_state_t s;
    spotify_service_state_cfg_t cfg = default_cfg();
    cfg.has_stored_creds = true;
    strncpy(cfg.stored_creds.refresh_token, "refresh-token-value", sizeof(cfg.stored_creds.refresh_token) - 1);
    cfg.stored_creds.paired_at_epoch_s = 1780000000;

    TEST_ASSERT_TRUE(spotify_service_state_init(&s, &cfg));
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_RESUMING, spotify_session_fsm_current_state(&s.session_fsm));
    /* Tier -1's own class of bug, applied here from the start: landing in
     * RESUMING alone never actually attempts a resume (core_fsm_init()
     * only sets current_state, no action runs) -- resume_pending is what
     * the drive loop's first iteration uses to really kick it off. */
    TEST_ASSERT_TRUE(s.resume_pending);
    TEST_ASSERT_EQUAL_STRING("refresh-token-value", s.refresh_token);
    TEST_ASSERT_EQUAL_INT64(1780000000, s.paired_at_epoch_s);
}

static void test_resume_pending_flag_drives_to_authorized(void)
{
    /* Simulates spotify_service_session_drive()'s own resume_pending
     * branch -- confirms the flag really does lead to a real resume
     * attempt once dispatched. */
    spotify_service_state_t s;
    spotify_service_state_cfg_t cfg = default_cfg();
    cfg.has_stored_creds = true;
    strncpy(cfg.stored_creds.refresh_token, "refresh-token-value", sizeof(cfg.stored_creds.refresh_token) - 1);
    spotify_service_state_init(&s, &cfg);

    TEST_ASSERT_TRUE(s.resume_pending);
    s.resume_pending = false;
    spotify_session_fsm_dispatch(&s.session_fsm, SPOTIFY_SESSION_EVT_RESUME_DUE, NULL);
    spotify_session_fsm_dispatch(&s.session_fsm, SPOTIFY_SESSION_EVT_RESUME_OK, NULL);

    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_AUTHORIZED, spotify_session_fsm_current_state(&s.session_fsm));
}

static void test_avr_pin_carried_from_cfg(void)
{
    spotify_service_state_t s;
    spotify_service_state_cfg_t cfg = default_cfg();
    strncpy(cfg.stored_avr_pin.avr_device_id, "dev-1", sizeof(cfg.stored_avr_pin.avr_device_id) - 1);
    strncpy(cfg.stored_avr_pin.avr_name, "Denon AVR-X3800H", sizeof(cfg.stored_avr_pin.avr_name) - 1);

    spotify_service_state_init(&s, &cfg);
    TEST_ASSERT_EQUAL_STRING("dev-1", s.avr_pin.avr_device_id);
    TEST_ASSERT_EQUAL_STRING("Denon AVR-X3800H", s.avr_pin.avr_name);
}

/* The actual restartability property: N create->teardown->create cycles,
 * with real activity (an advanced FSM, dirtied poll-scheduling fields)
 * injected in between, must leave the struct in exactly the same state as
 * a fresh init -- nothing leaks across a restart. */
static void test_repeated_init_reset_cycles_return_to_baseline(void)
{
    spotify_service_state_cfg_t cfg = default_cfg();

    spotify_service_state_t baseline;
    spotify_service_state_init(&baseline, &cfg);

    for (int cycle = 0; cycle < 5; cycle++) {
        spotify_service_state_t s;
        TEST_ASSERT_TRUE(spotify_service_state_init(&s, &cfg));

        /* Dirty it: advance the FSM, set poll/backoff bookkeeping. */
        spotify_session_fsm_dispatch(&s.session_fsm, SPOTIFY_SESSION_EVT_PAIR_REQUESTED, NULL);
        s.consecutive_poll_errors     = 3;
        s.next_poll_due_us              = 123456789;
        s.force_poll_index                = 1;
        s.token_op_just_ran                 = true;
        strncpy(s.pending_state, "deadbeef", sizeof(s.pending_state) - 1);

        char msg[32];
        snprintf(msg, sizeof(msg), "cycle %d", cycle);
        TEST_ASSERT_EQUAL_MESSAGE(SPOTIFY_SESSION_FSM_STATE_AWAITING_CODE,
            spotify_session_fsm_current_state(&s.session_fsm), msg);

        spotify_service_state_reset(&s);

        /* Re-init after reset must match a completely fresh instance. */
        TEST_ASSERT_TRUE_MESSAGE(spotify_service_state_init(&s, &cfg), msg);
        TEST_ASSERT_EQUAL_MESSAGE(spotify_session_fsm_current_state(&baseline.session_fsm),
            spotify_session_fsm_current_state(&s.session_fsm), msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(baseline.client_id, s.client_id, msg);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.consecutive_poll_errors, msg);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(0, s.next_poll_due_us, msg);
        TEST_ASSERT_EQUAL_MESSAGE(-1, s.force_poll_index, msg);
        TEST_ASSERT_FALSE_MESSAGE(s.token_op_just_ran, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", s.pending_state, msg);
    }
}

static void test_reset_zeroes_everything(void)
{
    spotify_service_state_t s;
    spotify_service_state_cfg_t cfg = default_cfg();
    spotify_service_state_init(&s, &cfg);

    s.token_op_just_ran = true;
    strncpy(s.access_token, "some-token", sizeof(s.access_token) - 1);

    spotify_service_state_reset(&s);

    spotify_service_state_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    TEST_ASSERT_EQUAL_MEMORY(&zeroed, &s, sizeof(s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_with_no_stored_creds_starts_unpaired);
    RUN_TEST(test_init_with_stored_creds_starts_resuming_with_pending_flag);
    RUN_TEST(test_resume_pending_flag_drives_to_authorized);
    RUN_TEST(test_avr_pin_carried_from_cfg);
    RUN_TEST(test_repeated_init_reset_cycles_return_to_baseline);
    RUN_TEST(test_reset_zeroes_everything);
    return UNITY_END();
}
