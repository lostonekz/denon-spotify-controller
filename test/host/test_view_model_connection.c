/*
 * Host test for view_model_connection.c -- the Connection screen's pure
 * builder. Same Tier-1 shape as test_view_model_denon.c.
 */
#include <string.h>

#include "unity.h"

#include "view_model_connection.h"

void setUp(void) {}
void tearDown(void) {}

static denon_state_t make_denon(link_status_t status, const char *host, uint16_t volume_limit_half_db,
                                    uint32_t round_trip_ms)
{
    denon_state_t denon = { 0 };
    denon.meta.status      = status;
    denon.meta.version       = 1;
    denon.volume_limit_half_db = volume_limit_half_db;
    denon.round_trip_ms          = round_trip_ms;
    if (host != NULL) {
        strncpy(denon.host, host, sizeof(denon.host) - 1);
    }
    return denon;
}

static void test_telnet_selected_when_link_ok(void)
{
    denon_state_t denon = make_denon(LINK_STATUS_OK, "192.168.1.15", 70, 38);

    connection_vm_t vm = view_model_connection_build(&denon);
    TEST_ASSERT_TRUE(vm.telnet_selected);
    TEST_ASSERT_FALSE(vm.http_selected);
    TEST_ASSERT_EQUAL(TONE_LIVE, vm.link_tone);
    TEST_ASSERT_EQUAL_STRING("Linked", vm.link_label);
}

static void test_http_selected_when_link_stale(void)
{
    denon_state_t denon = make_denon(LINK_STATUS_STALE, "192.168.1.15", 70, 38);

    connection_vm_t vm = view_model_connection_build(&denon);
    TEST_ASSERT_FALSE(vm.telnet_selected);
    TEST_ASSERT_TRUE(vm.http_selected);
    TEST_ASSERT_EQUAL_STRING("Fallback", vm.link_label);
}

static void test_host_passthrough(void)
{
    denon_state_t denon = make_denon(LINK_STATUS_OK, "192.168.1.15", 70, 38);

    connection_vm_t vm = view_model_connection_build(&denon);
    TEST_ASSERT_EQUAL_STRING("192.168.1.15", vm.host);
}

static void test_host_empty_shows_not_set(void)
{
    denon_state_t denon = make_denon(LINK_STATUS_OK, NULL, 70, 38);

    connection_vm_t vm = view_model_connection_build(&denon);
    TEST_ASSERT_EQUAL_STRING("Not set", vm.host);
}

static void test_round_trip_display(void)
{
    denon_state_t denon = make_denon(LINK_STATUS_OK, "192.168.1.15", 70, 38);

    connection_vm_t vm = view_model_connection_build(&denon);
    TEST_ASSERT_EQUAL_STRING("38 ms", vm.round_trip_display);
}

static void test_round_trip_never_measured_shows_dashes(void)
{
    denon_state_t denon = make_denon(LINK_STATUS_OK, "192.168.1.15", 70, 0);

    connection_vm_t vm = view_model_connection_build(&denon);
    TEST_ASSERT_EQUAL_STRING("--", vm.round_trip_display);
}

static void test_volume_limit_display(void)
{
    denon_state_t denon = make_denon(LINK_STATUS_OK, "192.168.1.15", 70, 38);   /* 35.0 dB */

    connection_vm_t vm = view_model_connection_build(&denon);
    TEST_ASSERT_EQUAL_STRING("35.0", vm.volume_limit_display);
    TEST_ASSERT_EQUAL_UINT16(70, vm.volume_limit_half_db_raw);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_telnet_selected_when_link_ok);
    RUN_TEST(test_http_selected_when_link_stale);
    RUN_TEST(test_host_passthrough);
    RUN_TEST(test_host_empty_shows_not_set);
    RUN_TEST(test_round_trip_display);
    RUN_TEST(test_round_trip_never_measured_shows_dashes);
    RUN_TEST(test_volume_limit_display);
    return UNITY_END();
}
