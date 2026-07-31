/*
 * Host test for denon_link_fsm: every transition in denon_link_fsm.h's
 * table, including the caller-chosen initial_state (ota_fsm-style, not
 * net_wifi_fsm's fixed BOOT) and the two "newly confirmed host" transitions
 * that fire both start_connect_telnet AND probe_capabilities, vs. every
 * routine-reconnect transition that fires start_connect_telnet alone.
 */

#include "unity.h"

#include "denon_link_fsm.h"

#define MAX_CALLS 16

typedef struct {
    const char *calls[MAX_CALLS];
    int         call_count;

    int  log_calls;
    uint32_t log_from, log_event, log_to;
} fixture_t;

static fixture_t g_fx;

static void record(const char *name)
{
    if (g_fx.call_count < MAX_CALLS) {
        g_fx.calls[g_fx.call_count++] = name;
    }
}

static void act_start_discovery(void *ctx)          { (void)ctx; record("start_discovery"); }
static void act_start_connect_telnet(void *ctx)      { (void)ctx; record("start_connect_telnet"); }
static void act_probe_capabilities(void *ctx)        { (void)ctx; record("probe_capabilities"); }
static void act_enter_connected_telnet(void *ctx)    { (void)ctx; record("enter_connected_telnet"); }
static void act_start_connect_http(void *ctx)        { (void)ctx; record("start_connect_http"); }
static void act_enter_connected_http(void *ctx)      { (void)ctx; record("enter_connected_http"); }
static void act_start_backoff_discovery(void *ctx)   { (void)ctx; record("start_backoff_discovery"); }
static void act_start_backoff_connect(void *ctx)     { (void)ctx; record("start_backoff_connect"); }
static void act_release_link(void *ctx)              { (void)ctx; record("release_link"); }

static void log_capture(void *ctx, const char *name, uint32_t from, uint32_t event, uint32_t to)
{
    (void)name;
    fixture_t *f = (fixture_t *)ctx;
    f->log_calls++;
    f->log_from  = from;
    f->log_event = event;
    f->log_to    = to;
}

static denon_link_fsm_t g_dfsm;

static void init_from(denon_link_state_t initial_state)
{
    g_fx = (fixture_t){ 0 };

    denon_link_fsm_actions_t actions = {
        .start_discovery          = act_start_discovery,
        .start_connect_telnet     = act_start_connect_telnet,
        .probe_capabilities       = act_probe_capabilities,
        .enter_connected_telnet   = act_enter_connected_telnet,
        .start_connect_http       = act_start_connect_http,
        .enter_connected_http     = act_enter_connected_http,
        .start_backoff_discovery  = act_start_backoff_discovery,
        .start_backoff_connect    = act_start_backoff_connect,
        .release_link             = act_release_link,
        .ctx                      = NULL,
    };
    denon_link_fsm_init(&g_dfsm, initial_state, &actions, log_capture, &g_fx);
}

void setUp(void) { init_from(DENON_LINK_STATE_IDLE); }
void tearDown(void) {}

static void assert_calls_1(const char *a)
{
    TEST_ASSERT_EQUAL(1, g_fx.call_count);
    TEST_ASSERT_EQUAL_STRING(a, g_fx.calls[0]);
}

static void assert_calls_2(const char *a, const char *b)
{
    TEST_ASSERT_EQUAL(2, g_fx.call_count);
    TEST_ASSERT_EQUAL_STRING(a, g_fx.calls[0]);
    TEST_ASSERT_EQUAL_STRING(b, g_fx.calls[1]);
}

/* --- caller-chosen initial_state (ota_fsm-style) --- */

static void test_initial_state_respects_caller_choice_idle(void)
{
    init_from(DENON_LINK_STATE_IDLE);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_IDLE, denon_link_fsm_current_state(&g_dfsm));
}

static void test_initial_state_respects_caller_choice_connecting_telnet(void)
{
    /* svc_denon_create() picks this when SETTING_DENON_HOST is already cached. */
    init_from(DENON_LINK_STATE_CONNECTING_TELNET);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTING_TELNET, denon_link_fsm_current_state(&g_dfsm));
}

/* --- IDLE / DISCOVERING --- */

static void test_idle_host_configured_connects_and_probes(void)
{
    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTING_TELNET, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_2("start_connect_telnet", "probe_capabilities");
}

static void test_idle_discovery_needed_goes_to_discovering(void)
{
    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_DISCOVERY_NEEDED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_DISCOVERING, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("start_discovery");
}

static void test_discovering_discovered_connects_and_probes(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_DISCOVERY_NEEDED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_DISCOVERED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTING_TELNET, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_2("start_connect_telnet", "probe_capabilities");
}

static void test_discovering_discovery_failed_goes_to_backoff_discovery(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_DISCOVERY_NEEDED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_DISCOVERY_FAILED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_BACKOFF_DISCOVERY, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("start_backoff_discovery");
}

/* --- telnet path, and its immediate (not backed-off) fallback --- */

static void test_connecting_telnet_connected(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_CONNECTED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTED_TELNET, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("enter_connected_telnet");
}

static void test_connecting_telnet_refused_falls_back_to_http_immediately(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_REFUSED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL_MESSAGE(DENON_LINK_STATE_CONNECTING_HTTP, denon_link_fsm_current_state(&g_dfsm),
                                "refusal must fall back to HTTP directly, never through backoff");
    assert_calls_1("start_connect_http");
}

static void test_connected_telnet_eof_falls_back_to_http_immediately(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_CONNECTED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_EOF, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTING_HTTP, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("start_connect_http");
}

/* --- HTTP fallback path --- */

static void test_connecting_http_connected(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_REFUSED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HTTP_CONNECTED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTED_HTTP, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("enter_connected_http");
}

static void test_connecting_http_failed_goes_to_backoff_connect(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_REFUSED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HTTP_FAILED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_BACKOFF_CONNECT, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("start_backoff_connect");
}

static void test_connected_http_host_unreachable_goes_to_discovering(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_REFUSED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HTTP_CONNECTED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_UNREACHABLE, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_DISCOVERING, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("start_discovery");
}

static void test_connected_http_promote_due_connects_telnet_without_reprobing(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_REFUSED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HTTP_CONNECTED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_PROMOTE_DUE, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTING_TELNET, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("start_connect_telnet");   /* no probe_capabilities -- not a newly-confirmed host */
}

/* --- the two split backoff states --- */

static void test_backoff_discovery_elapsed_goes_to_discovering(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_DISCOVERY_NEEDED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_DISCOVERY_FAILED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_BACKOFF_ELAPSED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_DISCOVERING, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("start_discovery");
}

static void test_backoff_connect_elapsed_connects_telnet_without_reprobing(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_REFUSED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HTTP_FAILED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_BACKOFF_ELAPSED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTING_TELNET, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("start_connect_telnet");
}

/* --- sleep/release/wake (no real producer until Phase 6 -- exercised synthetically) --- */

static void test_connected_telnet_sleep_enter_releases(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_CONNECTED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_SLEEP_ENTER, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_RELEASED, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("release_link");
}

static void test_connected_http_sleep_enter_releases(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_REFUSED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HTTP_CONNECTED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_SLEEP_ENTER, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_RELEASED, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("release_link");
}

static void test_released_wake_connects_telnet_without_reprobing(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_CONNECTED, NULL);
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_SLEEP_ENTER, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_WAKE, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTING_TELNET, denon_link_fsm_current_state(&g_dfsm));
    assert_calls_1("start_connect_telnet");
}

/* --- logging + undefined transitions --- */

static void test_on_transition_logs_from_event_to(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_DISCOVERY_NEEDED, NULL);

    TEST_ASSERT_EQUAL(1, g_fx.log_calls);
    TEST_ASSERT_EQUAL_UINT32(DENON_LINK_STATE_IDLE, g_fx.log_from);
    TEST_ASSERT_EQUAL_UINT32(DENON_LINK_EVT_DISCOVERY_NEEDED, g_fx.log_event);
    TEST_ASSERT_EQUAL_UINT32(DENON_LINK_STATE_DISCOVERING, g_fx.log_to);
}

static void test_undefined_transition_returns_no_transition_and_no_state_change(void)
{
    /* IDLE has no TELNET_CONNECTED entry. */
    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_TELNET_CONNECTED, NULL);

    TEST_ASSERT_EQUAL(FSM_NO_TRANSITION, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_IDLE, denon_link_fsm_current_state(&g_dfsm));
    TEST_ASSERT_EQUAL(0, g_fx.call_count);
    TEST_ASSERT_EQUAL(0, g_fx.log_calls);
}

/* PENDING_VERIFY-equivalent lesson from ota_fsm: while CONNECTING_TELNET
 * (mid-attempt), nothing but the three real outcomes moves the FSM --
 * e.g. a stray PROMOTE_DUE firing during the initial connect attempt must
 * be a no-op, not a spurious extra connect. */
static void test_connecting_telnet_ignores_promote_due(void)
{
    denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
    g_fx.call_count = 0;

    fsm_result_t r = denon_link_fsm_dispatch(&g_dfsm, DENON_LINK_EVT_PROMOTE_DUE, NULL);

    TEST_ASSERT_EQUAL(FSM_NO_TRANSITION, r);
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTING_TELNET, denon_link_fsm_current_state(&g_dfsm));
    TEST_ASSERT_EQUAL(0, g_fx.call_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_respects_caller_choice_idle);
    RUN_TEST(test_initial_state_respects_caller_choice_connecting_telnet);
    RUN_TEST(test_idle_host_configured_connects_and_probes);
    RUN_TEST(test_idle_discovery_needed_goes_to_discovering);
    RUN_TEST(test_discovering_discovered_connects_and_probes);
    RUN_TEST(test_discovering_discovery_failed_goes_to_backoff_discovery);
    RUN_TEST(test_connecting_telnet_connected);
    RUN_TEST(test_connecting_telnet_refused_falls_back_to_http_immediately);
    RUN_TEST(test_connected_telnet_eof_falls_back_to_http_immediately);
    RUN_TEST(test_connecting_http_connected);
    RUN_TEST(test_connecting_http_failed_goes_to_backoff_connect);
    RUN_TEST(test_connected_http_host_unreachable_goes_to_discovering);
    RUN_TEST(test_connected_http_promote_due_connects_telnet_without_reprobing);
    RUN_TEST(test_backoff_discovery_elapsed_goes_to_discovering);
    RUN_TEST(test_backoff_connect_elapsed_connects_telnet_without_reprobing);
    RUN_TEST(test_connected_telnet_sleep_enter_releases);
    RUN_TEST(test_connected_http_sleep_enter_releases);
    RUN_TEST(test_released_wake_connects_telnet_without_reprobing);
    RUN_TEST(test_on_transition_logs_from_event_to);
    RUN_TEST(test_undefined_transition_returns_no_transition_and_no_state_change);
    RUN_TEST(test_connecting_telnet_ignores_promote_due);
    return UNITY_END();
}
