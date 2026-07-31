/*
 * Host test for spotify_session_fsm: every transition in the table,
 * including the caller-chosen initial_state, the RESUMING/REFRESHING
 * split's different transient-failure landing states, EXCHANGE_FAILED
 * always landing on UNPAIRED, and UNPAIR_REQUESTED from every real state.
 */

#include "unity.h"

#include "spotify_session_fsm.h"

#define MAX_CALLS 16

typedef struct {
    const char *calls[MAX_CALLS];
    int         call_count;
} fixture_t;

static fixture_t g_fx;

static void record(const char *name)
{
    if (g_fx.call_count < MAX_CALLS) {
        g_fx.calls[g_fx.call_count++] = name;
    }
}

static void act_start_pair(void *ctx)                       { (void)ctx; record("start_pair"); }
static void act_start_exchange(void *ctx)                   { (void)ctx; record("start_exchange"); }
static void act_enter_authorized_first_time(void *ctx)      { (void)ctx; record("enter_authorized_first_time"); }
static void act_report_exchange_failed(void *ctx)           { (void)ctx; record("report_exchange_failed"); }
static void act_start_resume(void *ctx)                     { (void)ctx; record("start_resume"); }
static void act_enter_authorized_resumed(void *ctx)         { (void)ctx; record("enter_authorized_resumed"); }
static void act_enter_reauth_required(void *ctx)            { (void)ctx; record("enter_reauth_required"); }
static void act_report_resume_failed_transient(void *ctx)   { (void)ctx; record("report_resume_failed_transient"); }
static void act_start_refresh(void *ctx)                    { (void)ctx; record("start_refresh"); }
static void act_report_refresh_failed_transient(void *ctx)  { (void)ctx; record("report_refresh_failed_transient"); }
static void act_unpair(void *ctx)                           { (void)ctx; record("unpair"); }

static spotify_session_fsm_t g_sfsm;

static void init_from(spotify_session_fsm_state_t initial_state)
{
    g_fx = (fixture_t){ 0 };

    spotify_session_fsm_actions_t actions = {
        .start_pair                      = act_start_pair,
        .start_exchange                  = act_start_exchange,
        .enter_authorized_first_time     = act_enter_authorized_first_time,
        .report_exchange_failed          = act_report_exchange_failed,
        .start_resume                    = act_start_resume,
        .enter_authorized_resumed        = act_enter_authorized_resumed,
        .enter_reauth_required           = act_enter_reauth_required,
        .report_resume_failed_transient  = act_report_resume_failed_transient,
        .start_refresh                   = act_start_refresh,
        .report_refresh_failed_transient = act_report_refresh_failed_transient,
        .unpair                          = act_unpair,
        .ctx                             = NULL,
    };
    spotify_session_fsm_init(&g_sfsm, initial_state, &actions, NULL, NULL);
}

void setUp(void) { init_from(SPOTIFY_SESSION_FSM_STATE_UNPAIRED); }
void tearDown(void) {}

static void assert_calls_1(const char *a)
{
    TEST_ASSERT_EQUAL(1, g_fx.call_count);
    TEST_ASSERT_EQUAL_STRING(a, g_fx.calls[0]);
}

static void test_initial_state_is_caller_chosen(void)
{
    init_from(SPOTIFY_SESSION_FSM_STATE_RESUMING);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_RESUMING, spotify_session_fsm_current_state(&g_sfsm));

    init_from(SPOTIFY_SESSION_FSM_STATE_UNPAIRED);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_UNPAIRED, spotify_session_fsm_current_state(&g_sfsm));
}

static void test_pair_flow_to_authorized(void)
{
    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_PAIR_REQUESTED, NULL);
    assert_calls_1("start_pair");
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_AWAITING_CODE, spotify_session_fsm_current_state(&g_sfsm));

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_CODE_PASTED, NULL);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_EXCHANGING_CODE, spotify_session_fsm_current_state(&g_sfsm));

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_EXCHANGE_OK, NULL);
    TEST_ASSERT_EQUAL_STRING("enter_authorized_first_time", g_fx.calls[g_fx.call_count - 1]);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_AUTHORIZED, spotify_session_fsm_current_state(&g_sfsm));
}

static void test_exchange_failed_always_returns_to_unpaired(void)
{
    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_PAIR_REQUESTED, NULL);
    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_CODE_PASTED, NULL);

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_EXCHANGE_FAILED, NULL);
    TEST_ASSERT_EQUAL_STRING("report_exchange_failed", g_fx.calls[g_fx.call_count - 1]);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_UNPAIRED, spotify_session_fsm_current_state(&g_sfsm));
}

static void test_resume_ok(void)
{
    init_from(SPOTIFY_SESSION_FSM_STATE_RESUMING);

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_RESUME_DUE, NULL);
    assert_calls_1("start_resume");
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_RESUMING, spotify_session_fsm_current_state(&g_sfsm));

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_RESUME_OK, NULL);
    TEST_ASSERT_EQUAL_STRING("enter_authorized_resumed", g_fx.calls[g_fx.call_count - 1]);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_AUTHORIZED, spotify_session_fsm_current_state(&g_sfsm));
}

static void test_resume_failed_auth_goes_to_reauth_required(void)
{
    init_from(SPOTIFY_SESSION_FSM_STATE_RESUMING);
    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_RESUME_DUE, NULL);

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_RESUME_FAILED_AUTH, NULL);
    TEST_ASSERT_EQUAL_STRING("enter_reauth_required", g_fx.calls[g_fx.call_count - 1]);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_REAUTH_REQUIRED, spotify_session_fsm_current_state(&g_sfsm));
}

/* The RESUMING/REFRESHING split's whole reason to exist: a transient
 * failure while resuming (no usable token yet) stays in RESUMING, not
 * AUTHORIZED. */
static void test_resume_failed_transient_stays_in_resuming(void)
{
    init_from(SPOTIFY_SESSION_FSM_STATE_RESUMING);
    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_RESUME_DUE, NULL);

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_RESUME_FAILED_TRANSIENT, NULL);
    TEST_ASSERT_EQUAL_STRING("report_resume_failed_transient", g_fx.calls[g_fx.call_count - 1]);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_RESUMING, spotify_session_fsm_current_state(&g_sfsm));
}

static void test_refresh_ok(void)
{
    init_from(SPOTIFY_SESSION_FSM_STATE_AUTHORIZED);

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_REFRESH_DUE, NULL);
    assert_calls_1("start_refresh");
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_REFRESHING, spotify_session_fsm_current_state(&g_sfsm));

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_REFRESH_OK, NULL);
    TEST_ASSERT_EQUAL_STRING("enter_authorized_resumed", g_fx.calls[g_fx.call_count - 1]);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_AUTHORIZED, spotify_session_fsm_current_state(&g_sfsm));
}

static void test_refresh_failed_auth_goes_to_reauth_required(void)
{
    init_from(SPOTIFY_SESSION_FSM_STATE_AUTHORIZED);
    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_REFRESH_DUE, NULL);

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_REFRESH_FAILED_AUTH, NULL);
    TEST_ASSERT_EQUAL_STRING("enter_reauth_required", g_fx.calls[g_fx.call_count - 1]);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_REAUTH_REQUIRED, spotify_session_fsm_current_state(&g_sfsm));
}

/* The RESUMING/REFRESHING split's other half: a transient failure while
 * refreshing (old access token still valid a while longer) returns to
 * AUTHORIZED, not stuck in REFRESHING. */
static void test_refresh_failed_transient_returns_to_authorized(void)
{
    init_from(SPOTIFY_SESSION_FSM_STATE_AUTHORIZED);
    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_REFRESH_DUE, NULL);

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_REFRESH_FAILED_TRANSIENT, NULL);
    TEST_ASSERT_EQUAL_STRING("report_refresh_failed_transient", g_fx.calls[g_fx.call_count - 1]);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_AUTHORIZED, spotify_session_fsm_current_state(&g_sfsm));
}

static void test_repair_directly_from_reauth_required(void)
{
    init_from(SPOTIFY_SESSION_FSM_STATE_REAUTH_REQUIRED);

    spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_PAIR_REQUESTED, NULL);
    assert_calls_1("start_pair");
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_AWAITING_CODE, spotify_session_fsm_current_state(&g_sfsm));
}

static void test_unpair_from_every_real_state(void)
{
    spotify_session_fsm_state_t states[] = {
        SPOTIFY_SESSION_FSM_STATE_AWAITING_CODE,
        SPOTIFY_SESSION_FSM_STATE_EXCHANGING_CODE,
        SPOTIFY_SESSION_FSM_STATE_RESUMING,
        SPOTIFY_SESSION_FSM_STATE_AUTHORIZED,
        SPOTIFY_SESSION_FSM_STATE_REFRESHING,
        SPOTIFY_SESSION_FSM_STATE_REAUTH_REQUIRED,
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        init_from(states[i]);
        spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED, NULL);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("unpair", g_fx.calls[g_fx.call_count - 1], "state index");
        TEST_ASSERT_EQUAL_MESSAGE(SPOTIFY_SESSION_FSM_STATE_UNPAIRED,
            spotify_session_fsm_current_state(&g_sfsm), "state index");
    }
}

static void test_unmatched_event_is_no_op(void)
{
    /* UNPAIRED has no UNPAIR_REQUESTED row -- already unpaired. */
    fsm_result_t r = spotify_session_fsm_dispatch(&g_sfsm, SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED, NULL);
    TEST_ASSERT_EQUAL(FSM_NO_TRANSITION, r);
    TEST_ASSERT_EQUAL(0, g_fx.call_count);
    TEST_ASSERT_EQUAL(SPOTIFY_SESSION_FSM_STATE_UNPAIRED, spotify_session_fsm_current_state(&g_sfsm));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_caller_chosen);
    RUN_TEST(test_pair_flow_to_authorized);
    RUN_TEST(test_exchange_failed_always_returns_to_unpaired);
    RUN_TEST(test_resume_ok);
    RUN_TEST(test_resume_failed_auth_goes_to_reauth_required);
    RUN_TEST(test_resume_failed_transient_stays_in_resuming);
    RUN_TEST(test_refresh_ok);
    RUN_TEST(test_refresh_failed_auth_goes_to_reauth_required);
    RUN_TEST(test_refresh_failed_transient_returns_to_authorized);
    RUN_TEST(test_repair_directly_from_reauth_required);
    RUN_TEST(test_unpair_from_every_real_state);
    RUN_TEST(test_unmatched_event_is_no_op);
    return UNITY_END();
}
