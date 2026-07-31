/*
 * Host test for net_wifi_provisioning_fsm: the SoftAP portal lifecycle,
 * the failed-save retry loop, and the "recursive dispatch from inside an
 * action" pattern the real begin_save implementation is documented to use
 * to report an asynchronous NVS-write outcome.
 */

#include "unity.h"

#include <stdbool.h>

#include "net_wifi_provisioning_fsm.h"

#define MAX_CALLS 16

typedef struct {
    const char *calls[MAX_CALLS];
    int         call_count;
    const void *last_credentials;
} fixture_t;

static fixture_t g_fx;
static net_wifi_prov_fsm_t g_pfsm;   /* global so a recursive-dispatch test's action can reach it */

static void record(const char *name)
{
    if (g_fx.call_count < MAX_CALLS) {
        g_fx.calls[g_fx.call_count++] = name;
    }
}

static void act_start_ap(void *ctx)   { (void)ctx; record("start_ap"); }
static void act_ap_active(void *ctx)  { (void)ctx; record("ap_active"); }
static void act_save_failed(void *ctx){ (void)ctx; record("save_failed"); }
static void act_stop_ap(void *ctx)    { (void)ctx; record("stop_ap"); }
static void act_done(void *ctx)       { (void)ctx; record("done"); }
static void act_failed(void *ctx)     { (void)ctx; record("failed"); }

static void act_begin_save(void *ctx, const void *credentials)
{
    (void)ctx;
    record("begin_save");
    g_fx.last_credentials = credentials;
}

static net_wifi_prov_actions_t make_actions(void)
{
    net_wifi_prov_actions_t a = {
        .start_ap    = act_start_ap,
        .ap_active   = act_ap_active,
        .begin_save  = act_begin_save,
        .save_failed = act_save_failed,
        .stop_ap     = act_stop_ap,
        .done        = act_done,
        .failed      = act_failed,
        .ctx         = NULL,
    };
    return a;
}

void setUp(void)
{
    g_fx = (fixture_t){ 0 };
    net_wifi_prov_actions_t actions = make_actions();
    net_wifi_prov_fsm_init(&g_pfsm, &actions, NULL, NULL);
}

void tearDown(void) {}

static void test_initial_state_is_idle(void)
{
    TEST_ASSERT_EQUAL(PROV_STATE_IDLE, net_wifi_prov_fsm_current_state(&g_pfsm));
}

static void test_start_goes_to_ap_starting(void)
{
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_START, NULL);
    TEST_ASSERT_EQUAL(PROV_STATE_AP_STARTING, net_wifi_prov_fsm_current_state(&g_pfsm));
    TEST_ASSERT_EQUAL_STRING("start_ap", g_fx.calls[0]);
}

static void test_ap_start_failure_goes_to_failed(void)
{
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_START, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_AP_START_FAILED, NULL);
    TEST_ASSERT_EQUAL(PROV_STATE_FAILED, net_wifi_prov_fsm_current_state(&g_pfsm));
    TEST_ASSERT_EQUAL_STRING("failed", g_fx.calls[1]);
}

static void test_credentials_submitted_passes_event_data_through(void)
{
    static const char creds[] = "ssid=x;psk=y";

    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_START, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_AP_UP, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_CREDENTIALS_SUBMITTED, creds);

    TEST_ASSERT_EQUAL(PROV_STATE_SAVING, net_wifi_prov_fsm_current_state(&g_pfsm));
    TEST_ASSERT_EQUAL_PTR(creds, g_fx.last_credentials);
}

static void test_full_happy_path(void)
{
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_START, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_AP_UP, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_CREDENTIALS_SUBMITTED, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_SAVE_OK, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_AP_STOPPED, NULL);

    TEST_ASSERT_EQUAL(PROV_STATE_DONE, net_wifi_prov_fsm_current_state(&g_pfsm));

    const char *expect[] = { "start_ap", "ap_active", "begin_save", "stop_ap", "done" };
    TEST_ASSERT_EQUAL(5, g_fx.call_count);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_STRING(expect[i], g_fx.calls[i]);
    }
}

static void test_save_failed_returns_to_active_and_can_retry(void)
{
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_START, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_AP_UP, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_CREDENTIALS_SUBMITTED, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_SAVE_FAILED, NULL);

    TEST_ASSERT_EQUAL(PROV_STATE_AP_ACTIVE, net_wifi_prov_fsm_current_state(&g_pfsm));
    TEST_ASSERT_EQUAL_STRING("save_failed", g_fx.calls[g_fx.call_count - 1]);

    /* portal is still up -- a second submission can succeed */
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_CREDENTIALS_SUBMITTED, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_SAVE_OK, NULL);
    TEST_ASSERT_EQUAL(PROV_STATE_AP_STOPPING, net_wifi_prov_fsm_current_state(&g_pfsm));
}

static void test_outcome_reported_via_separate_later_dispatch(void)
{
    /* The correct pattern (see net_wifi_provisioning_fsm.h): begin_save is
     * just a notification; the real save happens in the caller's own code,
     * between two independent top-level dispatch calls -- never as a
     * recursive dispatch from inside the action itself, which would still
     * see the pre-commit state and silently find no transition. */
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_START, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_AP_UP, NULL);
    net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_CREDENTIALS_SUBMITTED, NULL);
    TEST_ASSERT_EQUAL(PROV_STATE_SAVING, net_wifi_prov_fsm_current_state(&g_pfsm));

    bool save_ok = true;   /* stands in for the caller's real net_wifi_credentials_save() */
    net_wifi_prov_fsm_dispatch(&g_pfsm, save_ok ? PROV_EVT_SAVE_OK : PROV_EVT_SAVE_FAILED, NULL);

    TEST_ASSERT_EQUAL(PROV_STATE_AP_STOPPING, net_wifi_prov_fsm_current_state(&g_pfsm));
}

static void test_undefined_transition_returns_no_transition_and_no_state_change(void)
{
    fsm_result_t r = net_wifi_prov_fsm_dispatch(&g_pfsm, PROV_EVT_AP_STOPPED, NULL);   /* IDLE has no such entry */

    TEST_ASSERT_EQUAL(FSM_NO_TRANSITION, r);
    TEST_ASSERT_EQUAL(PROV_STATE_IDLE, net_wifi_prov_fsm_current_state(&g_pfsm));
    TEST_ASSERT_EQUAL(0, g_fx.call_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_idle);
    RUN_TEST(test_start_goes_to_ap_starting);
    RUN_TEST(test_ap_start_failure_goes_to_failed);
    RUN_TEST(test_credentials_submitted_passes_event_data_through);
    RUN_TEST(test_full_happy_path);
    RUN_TEST(test_save_failed_returns_to_active_and_can_retry);
    RUN_TEST(test_outcome_reported_via_separate_later_dispatch);
    RUN_TEST(test_undefined_transition_returns_no_transition_and_no_state_change);
    return UNITY_END();
}
