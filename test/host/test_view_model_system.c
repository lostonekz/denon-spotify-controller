/*
 * Host test for view_model_system.c -- the System screen's pure builder.
 * Same Tier-1 shape as test_view_model_denon.c.
 */
#include <string.h>

#include "unity.h"

#include "fake_time_source.h"
#include "view_model_system.h"

static fake_time_source_t g_fake;
static time_source_t g_clock;

void setUp(void)
{
    g_fake.now_us = 0;
    g_clock = fake_time_source_get(&g_fake);
}

void tearDown(void) {}

static net_state_t make_net(link_status_t status, bool link_up)
{
    net_state_t net = { 0 };
    net.meta.status = status;
    net.link_up       = link_up;
    return net;
}

static denon_state_t make_denon(link_status_t status, uint32_t round_trip_ms)
{
    denon_state_t denon = { 0 };
    denon.meta.status     = status;
    denon.round_trip_ms = round_trip_ms;
    return denon;
}

static spotify_state_t make_spotify(spotify_session_summary_t status)
{
    spotify_state_t spotify = { 0 };
    spotify.session_status = status;
    return spotify;
}

static void test_wifi_shows_rssi_when_live(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true);
    denon_state_t    denon   = make_denon(LINK_STATUS_OK, 38);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_ACTIVE);

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, -48, "207b069", "Jul 30 2026", "A1B2C3", g_clock);
    TEST_ASSERT_EQUAL(TONE_LIVE, vm.wifi_tone);
    TEST_ASSERT_EQUAL_STRING("-48 dBm", vm.wifi_display);
}

static void test_wifi_shows_disconnected_when_offline(void)
{
    net_state_t     net     = make_net(LINK_STATUS_UNAVAILABLE, false);
    denon_state_t    denon   = make_denon(LINK_STATUS_OK, 38);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_ACTIVE);

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, 0, "207b069", "Jul 30 2026", "A1B2C3", g_clock);
    TEST_ASSERT_EQUAL(TONE_OFFLINE, vm.wifi_tone);
    TEST_ASSERT_EQUAL_STRING("Disconnected", vm.wifi_display);
}

static void test_denon_round_trip_display(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true);
    denon_state_t    denon   = make_denon(LINK_STATUS_OK, 38);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_ACTIVE);

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, -48, "207b069", "Jul 30 2026", "A1B2C3", g_clock);
    TEST_ASSERT_EQUAL(TONE_LIVE, vm.denon_tone);
    TEST_ASSERT_EQUAL_STRING("38 ms", vm.denon_display);
}

static void test_spotify_renewing_during_refresh(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true);
    denon_state_t    denon   = make_denon(LINK_STATUS_OK, 38);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_REFRESHING);

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, -48, "207b069", "Jul 30 2026", "A1B2C3", g_clock);
    TEST_ASSERT_EQUAL(TONE_STALE, vm.spotify_tone);
    TEST_ASSERT_EQUAL_STRING("Renewing", vm.spotify_display);
}

static void test_spotify_not_paired(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true);
    denon_state_t    denon   = make_denon(LINK_STATUS_OK, 38);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_UNPAIRED);

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, -48, "207b069", "Jul 30 2026", "A1B2C3", g_clock);
    TEST_ASSERT_EQUAL(TONE_OFFLINE, vm.spotify_tone);
    TEST_ASSERT_EQUAL_STRING("Not paired", vm.spotify_display);
}

static void test_verdict_positive_when_all_live(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true);
    denon_state_t    denon   = make_denon(LINK_STATUS_OK, 38);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_ACTIVE);

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, -48, "207b069", "Jul 30 2026", "A1B2C3", g_clock);
    TEST_ASSERT_EQUAL_STRING("Everything's working", vm.verdict);
}

static void test_verdict_needs_attention_when_any_not_live(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true);
    denon_state_t    denon   = make_denon(LINK_STATUS_STALE, 38);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_ACTIVE);

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, -48, "207b069", "Jul 30 2026", "A1B2C3", g_clock);
    TEST_ASSERT_EQUAL_STRING("Something needs attention", vm.verdict);
}

static void test_firmware_display_combines_version_and_date(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true);
    denon_state_t    denon   = make_denon(LINK_STATUS_OK, 38);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_ACTIVE);

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, -48, "207b069", "Jul 30 2026", "A1B2C3", g_clock);
    TEST_ASSERT_EQUAL_STRING("207b069 \xc2\xb7 Jul 30 2026", vm.firmware_display);
}

static void test_panel_id_passthrough(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true);
    denon_state_t    denon   = make_denon(LINK_STATUS_OK, 38);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_ACTIVE);

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, -48, "207b069", "Jul 30 2026", "A1B2C3", g_clock);
    TEST_ASSERT_EQUAL_STRING("A1B2C3", vm.panel_id);
}

static void test_uptime_from_clock(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true);
    denon_state_t    denon   = make_denon(LINK_STATUS_OK, 38);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_ACTIVE);

    g_fake.now_us = (int64_t)(2 * 3600 + 14 * 60) * 1000000;   /* 2h 14m since boot */
    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, -48, "207b069", "Jul 30 2026", "A1B2C3", g_clock);
    TEST_ASSERT_EQUAL_STRING("2 h 14 m", vm.uptime_display);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wifi_shows_rssi_when_live);
    RUN_TEST(test_wifi_shows_disconnected_when_offline);
    RUN_TEST(test_denon_round_trip_display);
    RUN_TEST(test_spotify_renewing_during_refresh);
    RUN_TEST(test_spotify_not_paired);
    RUN_TEST(test_verdict_positive_when_all_live);
    RUN_TEST(test_verdict_needs_attention_when_any_not_live);
    RUN_TEST(test_firmware_display_combines_version_and_date);
    RUN_TEST(test_panel_id_passthrough);
    RUN_TEST(test_uptime_from_clock);
    return UNITY_END();
}
