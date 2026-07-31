/*
 * Host test for net_wifi_fsm: every transition in docs/NETWORKING.md §1.1,
 * including the automatic DEGRADED->RECONNECTING chain that
 * net_wifi_fsm_dispatch() (not core_fsm_dispatch() directly) provides, and
 * the two failure/timeout paths (CONNECTING/SYNCING back to DEGRADED) the
 * source diagram implies but doesn't draw explicitly.
 */

#include "unity.h"

#include "net_wifi_fsm.h"

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

static void act_start_provisioning(void *ctx) { (void)ctx; record("start_provisioning"); }
static void act_start_connect(void *ctx)      { (void)ctx; record("start_connect"); }
static void act_enter_syncing(void *ctx)      { (void)ctx; record("enter_syncing"); }
static void act_enter_online(void *ctx)       { (void)ctx; record("enter_online"); }
static void act_enter_degraded(void *ctx)     { (void)ctx; record("enter_degraded"); }
static void act_start_backoff(void *ctx)      { (void)ctx; record("start_backoff"); }

static void log_capture(void *ctx, const char *name, uint32_t from, uint32_t event, uint32_t to)
{
    (void)name;
    fixture_t *f = (fixture_t *)ctx;
    f->log_calls++;
    f->log_from  = from;
    f->log_event = event;
    f->log_to    = to;
}

static net_wifi_fsm_t g_nfsm;

void setUp(void)
{
    g_fx = (fixture_t){ 0 };

    net_wifi_fsm_actions_t actions = {
        .start_provisioning = act_start_provisioning,
        .start_connect      = act_start_connect,
        .enter_syncing      = act_enter_syncing,
        .enter_online       = act_enter_online,
        .enter_degraded     = act_enter_degraded,
        .start_backoff      = act_start_backoff,
        .ctx                = NULL,
    };
    net_wifi_fsm_init(&g_nfsm, &actions, log_capture, &g_fx);
}

void tearDown(void) {}

static void test_initial_state_is_boot(void)
{
    TEST_ASSERT_EQUAL(NET_WIFI_STATE_BOOT, net_wifi_fsm_current_state(&g_nfsm));
}

static void test_boot_no_credentials_goes_to_provisioning(void)
{
    TEST_ASSERT_EQUAL(FSM_OK, net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_NO_CREDENTIALS, NULL));
    TEST_ASSERT_EQUAL(NET_WIFI_STATE_PROVISIONING, net_wifi_fsm_current_state(&g_nfsm));
    TEST_ASSERT_EQUAL(1, g_fx.call_count);
    TEST_ASSERT_EQUAL_STRING("start_provisioning", g_fx.calls[0]);
}

static void test_boot_with_credentials_skips_provisioning(void)
{
    TEST_ASSERT_EQUAL(FSM_OK, net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_HAVE_CREDENTIALS, NULL));
    TEST_ASSERT_EQUAL(NET_WIFI_STATE_CONNECTING, net_wifi_fsm_current_state(&g_nfsm));
    TEST_ASSERT_EQUAL_STRING("start_connect", g_fx.calls[0]);
}

static void test_provisioning_saved_goes_to_connecting(void)
{
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_NO_CREDENTIALS, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_CREDENTIALS_SAVED, NULL);
    TEST_ASSERT_EQUAL(NET_WIFI_STATE_CONNECTING, net_wifi_fsm_current_state(&g_nfsm));
}

static void test_full_happy_path_via_provisioning(void)
{
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_NO_CREDENTIALS, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_CREDENTIALS_SAVED, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_GOT_IP, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_SNTP_OK, NULL);

    TEST_ASSERT_EQUAL(NET_WIFI_STATE_ONLINE, net_wifi_fsm_current_state(&g_nfsm));

    const char *expect[] = { "start_provisioning", "start_connect", "enter_syncing", "enter_online" };
    TEST_ASSERT_EQUAL(4, g_fx.call_count);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_STRING(expect[i], g_fx.calls[i]);
    }
}

static void test_full_happy_path_with_stored_credentials(void)
{
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_HAVE_CREDENTIALS, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_GOT_IP, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_SNTP_OK, NULL);

    TEST_ASSERT_EQUAL(NET_WIFI_STATE_ONLINE, net_wifi_fsm_current_state(&g_nfsm));
    TEST_ASSERT_EQUAL(3, g_fx.call_count);   /* no provisioning step at all */
}

static void test_link_lost_auto_chains_to_reconnecting(void)
{
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_HAVE_CREDENTIALS, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_GOT_IP, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_SNTP_OK, NULL);
    g_fx.call_count = 0;   /* reset -- only care about the LINK_LOST dispatch below */

    fsm_result_t r = net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_LINK_LOST, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(NET_WIFI_STATE_RECONNECTING, net_wifi_fsm_current_state(&g_nfsm));
    TEST_ASSERT_EQUAL(2, g_fx.call_count);
    TEST_ASSERT_EQUAL_STRING("enter_degraded", g_fx.calls[0]);
    TEST_ASSERT_EQUAL_STRING("start_backoff", g_fx.calls[1]);
}

static void test_retry_now_returns_to_connecting(void)
{
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_HAVE_CREDENTIALS, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_GOT_IP, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_SNTP_OK, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_LINK_LOST, NULL);

    fsm_result_t r = net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_RETRY_NOW, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(NET_WIFI_STATE_CONNECTING, net_wifi_fsm_current_state(&g_nfsm));
}

static void test_connect_failed_from_connecting_chains_to_reconnecting(void)
{
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_HAVE_CREDENTIALS, NULL);

    fsm_result_t r = net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_CONNECT_FAILED, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(NET_WIFI_STATE_RECONNECTING, net_wifi_fsm_current_state(&g_nfsm));
}

static void test_sntp_timeout_from_syncing_chains_to_reconnecting(void)
{
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_HAVE_CREDENTIALS, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_GOT_IP, NULL);

    fsm_result_t r = net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_SNTP_TIMEOUT, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(NET_WIFI_STATE_RECONNECTING, net_wifi_fsm_current_state(&g_nfsm));
}

static void test_link_lost_from_syncing_also_chains_to_reconnecting(void)
{
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_HAVE_CREDENTIALS, NULL);
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_GOT_IP, NULL);

    fsm_result_t r = net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_LINK_LOST, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(NET_WIFI_STATE_RECONNECTING, net_wifi_fsm_current_state(&g_nfsm));
}

static void test_on_transition_logs_from_event_to(void)
{
    net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_NO_CREDENTIALS, NULL);

    TEST_ASSERT_EQUAL(1, g_fx.log_calls);
    TEST_ASSERT_EQUAL_UINT32(NET_WIFI_STATE_BOOT, g_fx.log_from);
    TEST_ASSERT_EQUAL_UINT32(NET_WIFI_EVT_NO_CREDENTIALS, g_fx.log_event);
    TEST_ASSERT_EQUAL_UINT32(NET_WIFI_STATE_PROVISIONING, g_fx.log_to);
}

static void test_undefined_transition_returns_no_transition_and_no_state_change(void)
{
    fsm_result_t r = net_wifi_fsm_dispatch(&g_nfsm, NET_WIFI_EVT_GOT_IP, NULL);   /* BOOT has no GOT_IP entry */

    TEST_ASSERT_EQUAL(FSM_NO_TRANSITION, r);
    TEST_ASSERT_EQUAL(NET_WIFI_STATE_BOOT, net_wifi_fsm_current_state(&g_nfsm));
    TEST_ASSERT_EQUAL(0, g_fx.call_count);
    TEST_ASSERT_EQUAL(0, g_fx.log_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_boot);
    RUN_TEST(test_boot_no_credentials_goes_to_provisioning);
    RUN_TEST(test_boot_with_credentials_skips_provisioning);
    RUN_TEST(test_provisioning_saved_goes_to_connecting);
    RUN_TEST(test_full_happy_path_via_provisioning);
    RUN_TEST(test_full_happy_path_with_stored_credentials);
    RUN_TEST(test_link_lost_auto_chains_to_reconnecting);
    RUN_TEST(test_retry_now_returns_to_connecting);
    RUN_TEST(test_connect_failed_from_connecting_chains_to_reconnecting);
    RUN_TEST(test_sntp_timeout_from_syncing_chains_to_reconnecting);
    RUN_TEST(test_link_lost_from_syncing_also_chains_to_reconnecting);
    RUN_TEST(test_on_transition_logs_from_event_to);
    RUN_TEST(test_undefined_transition_returns_no_transition_and_no_state_change);
    return UNITY_END();
}
