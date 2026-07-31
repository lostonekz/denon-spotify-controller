/*
 * Restartability test for svc_denon's pure state (denon_service_state.h).
 * LIFECYCLE.md §1.2: a restartable service needs a host test that runs
 * create->teardown->create in a loop and asserts state returns to
 * baseline. See denon_service_state.h's top comment for why THIS is the
 * piece that can be host-tested, not svc_denon_t itself (which embeds a
 * net_http_t* and is therefore FreeRTOS-tainted like every other svc_*.c
 * in this codebase).
 */

#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "denon_service_state.h"

void setUp(void) {}
void tearDown(void) {}

static denon_service_state_cfg_t default_cfg(void)
{
    denon_service_state_cfg_t cfg = { 0 };
    cfg.transport_pref       = DENON_LINK_TRANSPORT_AUTO;
    cfg.volume_limit_half_db = 140;
    return cfg;
}

static void test_init_with_no_host_starts_idle(void)
{
    denon_service_state_t s;
    denon_service_state_cfg_t cfg = default_cfg();

    TEST_ASSERT_TRUE(denon_service_state_init(&s, &cfg));
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_IDLE, denon_link_fsm_current_state(&s.link_fsm));
    TEST_ASSERT_EQUAL_STRING("", s.host);
}

/* Tier -1 fix: a cached host must NOT land directly in CONNECTING_TELNET.
 * core_fsm_init() only sets current_state, it never runs a transition's
 * action -- landing there directly would never actually call
 * telnet_iface.open() (nothing dispatches an event from that state on its
 * own; see denon_service_link.c's drive loop). init() instead starts IDLE
 * and sets host_changed_pending, which the drive loop's existing branch
 * turns into a real HOST_CONFIGURED dispatch. */
static void test_init_with_cached_host_starts_idle_with_pending_host_change(void)
{
    denon_service_state_t s;
    denon_service_state_cfg_t cfg = default_cfg();
    strncpy(cfg.initial_host, "192.168.1.50", sizeof(cfg.initial_host) - 1);

    TEST_ASSERT_TRUE(denon_service_state_init(&s, &cfg));
    TEST_ASSERT_EQUAL(DENON_LINK_STATE_IDLE, denon_link_fsm_current_state(&s.link_fsm));
    TEST_ASSERT_TRUE(s.host_changed_pending);
    TEST_ASSERT_EQUAL_STRING("192.168.1.50", s.host);
}

/* Simulates denon_service_link_drive()'s own IDLE + host_changed_pending
 * branch (denon_service_link.c) -- confirms the pending flag set above
 * really does lead to a real CONNECTING_TELNET attempt once dispatched,
 * closing the loop on the boot-dead-end bug rather than just asserting the
 * flag was set. */
static void test_cached_host_pending_flag_drives_to_connecting_telnet(void)
{
    denon_service_state_t s;
    denon_service_state_cfg_t cfg = default_cfg();
    strncpy(cfg.initial_host, "192.168.1.50", sizeof(cfg.initial_host) - 1);
    denon_service_state_init(&s, &cfg);

    TEST_ASSERT_TRUE(s.host_changed_pending);
    s.host_changed_pending = false;
    denon_link_fsm_dispatch(&s.link_fsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);

    TEST_ASSERT_EQUAL(DENON_LINK_STATE_CONNECTING_TELNET, denon_link_fsm_current_state(&s.link_fsm));
}

static void test_init_carries_settings_snapshot(void)
{
    denon_service_state_t s;
    denon_service_state_cfg_t cfg = default_cfg();
    cfg.transport_pref       = DENON_LINK_TRANSPORT_HTTP;
    cfg.volume_limit_half_db = 70;

    denon_service_state_init(&s, &cfg);

    TEST_ASSERT_EQUAL(DENON_LINK_TRANSPORT_HTTP, s.transport_pref);
    TEST_ASSERT_EQUAL_UINT16(70, s.volume_limit_half_db);
}

/* The actual restartability property: N create->teardown->create cycles,
 * with real activity (a queued command, a dispatched FSM transition,
 * dirtied driver flags) injected in between, must leave the struct in
 * exactly the same state as a fresh init -- nothing leaks across a
 * restart. */
static void test_repeated_init_reset_cycles_return_to_baseline(void)
{
    denon_service_state_cfg_t cfg = default_cfg();
    strncpy(cfg.initial_host, "192.168.1.50", sizeof(cfg.initial_host) - 1);

    denon_service_state_t baseline;
    denon_service_state_init(&baseline, &cfg);

    for (int cycle = 0; cycle < 5; cycle++) {
        denon_service_state_t s;
        TEST_ASSERT_TRUE(denon_service_state_init(&s, &cfg));

        /* Dirty it: queue commands, advance the FSM, set driver flags.
         * Init lands in IDLE with host_changed_pending set (Tier -1 fix) --
         * drive it through HOST_CONFIGURED first, same as the real drive
         * loop would, before TELNET_CONNECTED is a valid transition. */
        denon_cmdq_submit_discrete(&s.cmdq, "PWON");
        denon_cmdq_submit_absolute(&s.cmdq, DENON_CMD_CLASS_MV, "MV525");
        TEST_ASSERT_TRUE(s.host_changed_pending);
        s.host_changed_pending = false;
        denon_link_fsm_dispatch(&s.link_fsm, DENON_LINK_EVT_HOST_CONFIGURED, NULL);
        denon_link_fsm_dispatch(&s.link_fsm, DENON_LINK_EVT_TELNET_CONNECTED, NULL);
        s.discovery_just_ran = true;
        s.host_changed_pending = true;
        s.backoff_deadline_us = 123456789;
        s.local_state.volume_half_db = 99;

        char msg[32];
        snprintf(msg, sizeof(msg), "cycle %d", cycle);
        TEST_ASSERT_EQUAL_MESSAGE(DENON_LINK_STATE_CONNECTED_TELNET,
            denon_link_fsm_current_state(&s.link_fsm), msg);

        denon_service_state_reset(&s);

        /* Re-init after reset must match a completely fresh instance. */
        TEST_ASSERT_TRUE_MESSAGE(denon_service_state_init(&s, &cfg), msg);
        TEST_ASSERT_EQUAL_MESSAGE(denon_link_fsm_current_state(&baseline.link_fsm),
            denon_link_fsm_current_state(&s.link_fsm), msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(baseline.host, s.host, msg);
        TEST_ASSERT_EQUAL_MESSAGE(baseline.transport_pref, s.transport_pref, msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(baseline.volume_limit_half_db, s.volume_limit_half_db, msg);
        TEST_ASSERT_FALSE_MESSAGE(s.discovery_just_ran, msg);
        /* A fresh init with this cfg's cached host sets host_changed_pending
         * true again (Tier -1 fix -- this is what actually drives the boot
         * connection attempt), matching baseline rather than false. */
        TEST_ASSERT_EQUAL_MESSAGE(baseline.host_changed_pending, s.host_changed_pending, msg);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(0, s.backoff_deadline_us, msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, s.local_state.volume_half_db, msg);

        /* The cmdq must be empty too -- nothing from the dirtied cycle
         * survives into the next one. */
        denon_cmd_t out;
        TEST_ASSERT_FALSE_MESSAGE(denon_cmdq_pop_due(&s.cmdq, 0, &out), msg);
    }
}

static void test_reset_zeroes_everything(void)
{
    denon_service_state_t s;
    denon_service_state_cfg_t cfg = default_cfg();
    denon_service_state_init(&s, &cfg);

    denon_cmdq_submit_discrete(&s.cmdq, "MUON");
    s.telnet_open_ok = true;

    denon_service_state_reset(&s);

    denon_service_state_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    TEST_ASSERT_EQUAL_MEMORY(&zeroed, &s, sizeof(s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_with_no_host_starts_idle);
    RUN_TEST(test_init_with_cached_host_starts_idle_with_pending_host_change);
    RUN_TEST(test_cached_host_pending_flag_drives_to_connecting_telnet);
    RUN_TEST(test_init_carries_settings_snapshot);
    RUN_TEST(test_repeated_init_reset_cycles_return_to_baseline);
    RUN_TEST(test_reset_zeroes_everything);
    return UNITY_END();
}
