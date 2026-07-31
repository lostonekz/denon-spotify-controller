/*
 * Host test for ota_fsm: IDLE->CHECKING->DOWNLOADING->REBOOT_PENDING on the
 * happy path; the boot-time PENDING_VERIFY initial_state resolving via
 * GATE_PASS/GATE_TIMED_OUT; and that PENDING_VERIFY has no transition for
 * anything else (blocks every other state's entry "by construction").
 */

#include "unity.h"

#include "ota_fsm.h"

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

static void act_mark_valid(void *ctx)              { (void)ctx; record("mark_valid_cancel_rollback"); }
static void act_mark_invalid_reboot(void *ctx)     { (void)ctx; record("mark_invalid_rollback_and_reboot"); }
static void act_start_manifest_check(void *ctx)    { (void)ctx; record("start_manifest_check"); }
static void act_start_download(void *ctx)          { (void)ctx; record("start_download"); }
static void act_report_check_failed(void *ctx)     { (void)ctx; record("report_check_failed"); }
static void act_set_boot_partition(void *ctx)       { (void)ctx; record("set_boot_partition_and_reboot"); }
static void act_report_download_failed(void *ctx)  { (void)ctx; record("report_download_failed"); }

static ota_fsm_t g_ofsm;

static void init_at(ota_fsm_state_t initial_state)
{
    g_fx = (fixture_t){ 0 };

    ota_fsm_actions_t actions = {
        .mark_valid_cancel_rollback       = act_mark_valid,
        .mark_invalid_rollback_and_reboot = act_mark_invalid_reboot,
        .start_manifest_check             = act_start_manifest_check,
        .start_download                   = act_start_download,
        .report_check_failed              = act_report_check_failed,
        .set_boot_partition_and_reboot    = act_set_boot_partition,
        .report_download_failed           = act_report_download_failed,
        .ctx                              = NULL,
    };
    ota_fsm_init(&g_ofsm, initial_state, &actions, NULL, NULL);
}

void setUp(void)   { init_at(OTA_FSM_STATE_IDLE); }
void tearDown(void) {}

static void test_initial_state_is_whatever_caller_chose(void)
{
    init_at(OTA_FSM_STATE_IDLE);
    TEST_ASSERT_EQUAL(OTA_FSM_STATE_IDLE, ota_fsm_current_state(&g_ofsm));

    init_at(OTA_FSM_STATE_PENDING_VERIFY);
    TEST_ASSERT_EQUAL(OTA_FSM_STATE_PENDING_VERIFY, ota_fsm_current_state(&g_ofsm));
}

static void test_happy_path_idle_to_reboot_pending(void)
{
    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_CHECK_DUE, NULL);
    TEST_ASSERT_EQUAL(OTA_FSM_STATE_CHECKING, ota_fsm_current_state(&g_ofsm));

    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_MANIFEST_ADMITTED, NULL);
    TEST_ASSERT_EQUAL(OTA_FSM_STATE_DOWNLOADING, ota_fsm_current_state(&g_ofsm));

    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_IMAGE_ADMITTED, NULL);
    TEST_ASSERT_EQUAL(OTA_FSM_STATE_REBOOT_PENDING, ota_fsm_current_state(&g_ofsm));

    const char *expect[] = { "start_manifest_check", "start_download", "set_boot_partition_and_reboot" };
    TEST_ASSERT_EQUAL(3, g_fx.call_count);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_STRING(expect[i], g_fx.calls[i]);
    }
}

static void test_manifest_rejected_returns_to_idle(void)
{
    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_CHECK_DUE, NULL);
    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_MANIFEST_REJECTED, NULL);

    TEST_ASSERT_EQUAL(OTA_FSM_STATE_IDLE, ota_fsm_current_state(&g_ofsm));
    TEST_ASSERT_EQUAL_STRING("report_check_failed", g_fx.calls[1]);
}

static void test_image_rejected_returns_to_idle(void)
{
    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_CHECK_DUE, NULL);
    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_MANIFEST_ADMITTED, NULL);
    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_IMAGE_REJECTED, NULL);

    TEST_ASSERT_EQUAL(OTA_FSM_STATE_IDLE, ota_fsm_current_state(&g_ofsm));
    TEST_ASSERT_EQUAL_STRING("report_download_failed", g_fx.calls[2]);
}

static void test_download_transport_failure_returns_to_idle(void)
{
    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_CHECK_DUE, NULL);
    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_MANIFEST_ADMITTED, NULL);
    ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_DOWNLOAD_FAILED, NULL);

    TEST_ASSERT_EQUAL(OTA_FSM_STATE_IDLE, ota_fsm_current_state(&g_ofsm));
}

static void test_pending_verify_gate_pass_goes_to_idle(void)
{
    init_at(OTA_FSM_STATE_PENDING_VERIFY);

    fsm_result_t r = ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_GATE_PASS, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(OTA_FSM_STATE_IDLE, ota_fsm_current_state(&g_ofsm));
    TEST_ASSERT_EQUAL_STRING("mark_valid_cancel_rollback", g_fx.calls[0]);
}

static void test_pending_verify_gate_timed_out_goes_to_rolling_back(void)
{
    init_at(OTA_FSM_STATE_PENDING_VERIFY);

    fsm_result_t r = ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_GATE_TIMED_OUT, NULL);

    TEST_ASSERT_EQUAL(FSM_OK, r);
    TEST_ASSERT_EQUAL(OTA_FSM_STATE_ROLLING_BACK, ota_fsm_current_state(&g_ofsm));
    TEST_ASSERT_EQUAL_STRING("mark_invalid_rollback_and_reboot", g_fx.calls[0]);
}

static void test_pending_verify_blocks_every_other_event(void)
{
    init_at(OTA_FSM_STATE_PENDING_VERIFY);

    fsm_result_t r = ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_CHECK_DUE, NULL);

    TEST_ASSERT_EQUAL(FSM_NO_TRANSITION, r);
    TEST_ASSERT_EQUAL(OTA_FSM_STATE_PENDING_VERIFY, ota_fsm_current_state(&g_ofsm));
    TEST_ASSERT_EQUAL(0, g_fx.call_count);
}

static void test_undefined_transition_returns_no_transition_and_no_state_change(void)
{
    fsm_result_t r = ota_fsm_dispatch(&g_ofsm, OTA_FSM_EVT_MANIFEST_ADMITTED, NULL);   /* IDLE has no such entry */

    TEST_ASSERT_EQUAL(FSM_NO_TRANSITION, r);
    TEST_ASSERT_EQUAL(OTA_FSM_STATE_IDLE, ota_fsm_current_state(&g_ofsm));
    TEST_ASSERT_EQUAL(0, g_fx.call_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_whatever_caller_chose);
    RUN_TEST(test_happy_path_idle_to_reboot_pending);
    RUN_TEST(test_manifest_rejected_returns_to_idle);
    RUN_TEST(test_image_rejected_returns_to_idle);
    RUN_TEST(test_download_transport_failure_returns_to_idle);
    RUN_TEST(test_pending_verify_gate_pass_goes_to_idle);
    RUN_TEST(test_pending_verify_gate_timed_out_goes_to_rolling_back);
    RUN_TEST(test_pending_verify_blocks_every_other_event);
    RUN_TEST(test_undefined_transition_returns_no_transition_and_no_state_change);
    return UNITY_END();
}
