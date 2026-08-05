/*
 * Host test for view_model_pair.c -- the Spotify pair overlay's pure builder.
 * Same Tier-1 shape as test_view_model_system.c.
 *
 * Two of these exist specifically to pin bugs that are invisible on screen:
 * the IP byte order (a reversed address still "looks like an IP"), and the
 * fact that net_state_t.link_up is a write-once latch that never goes false.
 */
#include <string.h>

#include "unity.h"

#include "view_model_pair.h"

#define TEST_PORT 8080

void setUp(void) {}
void tearDown(void) {}

/* 192.168.1.23 as ip_info.ip.addr actually stores it: first octet in the low
 * byte. A >>24-first formatter renders this "23.1.168.192". */
#define IP_192_168_1_23 0x1701A8C0u

static net_state_t make_net(link_status_t status, bool link_up, uint32_t ipv4)
{
    net_state_t net = { 0 };
    net.meta.status = status;
    net.link_up     = link_up;
    net.ipv4        = ipv4;
    return net;
}

static spotify_state_t make_spotify(spotify_session_summary_t status)
{
    spotify_state_t spotify = { 0 };
    spotify.session_status = status;
    return spotify;
}

static void test_url_uses_esp_idf_octet_order(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, IP_192_168_1_23);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_PAIRING);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, true, false);
    TEST_ASSERT_TRUE(vm.show_qr);
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.23:8080/pair", vm.url);
}

/* link_up is latched true from a previous association; meta.status is the
 * only field that reports the network actually going away. */
static void test_stale_link_up_latch_does_not_yield_a_qr(void)
{
    net_state_t     net     = make_net(LINK_STATUS_UNAVAILABLE, true, IP_192_168_1_23);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_PAIRING);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, true, false);
    TEST_ASSERT_EQUAL(PAIR_UI_NO_NETWORK, vm.stage);
    TEST_ASSERT_FALSE(vm.show_qr);
    TEST_ASSERT_EQUAL_STRING("", vm.url);
    TEST_ASSERT_EQUAL(TONE_OFFLINE, vm.tone);
}

static void test_no_ip_yields_no_network(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, 0);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_PAIRING);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, true, false);
    TEST_ASSERT_EQUAL(PAIR_UI_NO_NETWORK, vm.stage);
    TEST_ASSERT_FALSE(vm.show_qr);
}

/* The QR must never advertise a port with nothing behind it. */
static void test_no_qr_until_the_listener_is_up(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, IP_192_168_1_23);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_PAIRING);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, false, false);
    TEST_ASSERT_EQUAL(PAIR_UI_WAITING, vm.stage);
    TEST_ASSERT_FALSE(vm.show_qr);
    TEST_ASSERT_EQUAL_STRING("", vm.url);
}

static void test_pairing_waits_for_the_paste(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, IP_192_168_1_23);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_PAIRING);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, true, false);
    TEST_ASSERT_EQUAL(PAIR_UI_WAITING, vm.stage);
    TEST_ASSERT_EQUAL(TONE_LIVE, vm.tone);
    TEST_ASSERT_FALSE(vm.show_confirm_button);
}

static void test_reauth_required_is_ready_not_failed(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, IP_192_168_1_23);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_REAUTH_REQUIRED);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, true, false);
    TEST_ASSERT_EQUAL(PAIR_UI_READY, vm.stage);
}

static void test_unpaired_before_pairing_is_ready(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, IP_192_168_1_23);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_UNPAIRED);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, true, false);
    TEST_ASSERT_EQUAL(PAIR_UI_READY, vm.stage);
}

/* Same session_status as the test above -- only the latch tells them apart. */
static void test_unpaired_after_pairing_is_a_failed_exchange(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, IP_192_168_1_23);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_UNPAIRED);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, true, true);
    TEST_ASSERT_EQUAL(PAIR_UI_FAILED, vm.stage);
    TEST_ASSERT_EQUAL(TONE_OFFLINE, vm.tone);
    TEST_ASSERT_FALSE(vm.show_qr);
}

static void test_active_after_pairing_is_connected(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, IP_192_168_1_23);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_ACTIVE);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, true, true);
    TEST_ASSERT_EQUAL(PAIR_UI_CONNECTED, vm.stage);
    TEST_ASSERT_EQUAL(TONE_LIVE, vm.tone);
    TEST_ASSERT_FALSE(vm.show_confirm_button);
}

static void test_active_without_having_paired_needs_confirm(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, IP_192_168_1_23);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_ACTIVE);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, true, false);
    TEST_ASSERT_EQUAL(PAIR_UI_NEEDS_CONFIRM, vm.stage);
    TEST_ASSERT_TRUE(vm.show_confirm_button);
    TEST_ASSERT_FALSE(vm.show_qr);
}

static void test_refreshing_is_a_working_session_too(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, IP_192_168_1_23);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_REFRESHING);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, TEST_PORT, true, false);
    TEST_ASSERT_EQUAL(PAIR_UI_NEEDS_CONFIRM, vm.stage);
    TEST_ASSERT_TRUE(vm.show_confirm_button);
}

static void test_longest_possible_url_is_not_truncated(void)
{
    net_state_t     net     = make_net(LINK_STATUS_OK, true, 0xFFFFFFFFu);
    spotify_state_t spotify = make_spotify(SPOTIFY_SESSION_PAIRING);

    pair_vm_t vm = view_model_pair_build(&net, &spotify, 65535, true, false);
    TEST_ASSERT_EQUAL_STRING("http://255.255.255.255:65535/pair", vm.url);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_url_uses_esp_idf_octet_order);
    RUN_TEST(test_stale_link_up_latch_does_not_yield_a_qr);
    RUN_TEST(test_no_ip_yields_no_network);
    RUN_TEST(test_no_qr_until_the_listener_is_up);
    RUN_TEST(test_pairing_waits_for_the_paste);
    RUN_TEST(test_reauth_required_is_ready_not_failed);
    RUN_TEST(test_unpaired_before_pairing_is_ready);
    RUN_TEST(test_unpaired_after_pairing_is_a_failed_exchange);
    RUN_TEST(test_active_after_pairing_is_connected);
    RUN_TEST(test_active_without_having_paired_needs_confirm);
    RUN_TEST(test_refreshing_is_a_working_session_too);
    RUN_TEST(test_longest_possible_url_is_not_truncated);
    return UNITY_END();
}
