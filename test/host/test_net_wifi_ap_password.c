/*
 * Host test for net_wifi_ap_password: deterministic, correct length,
 * hex+prefix charset, and a known-vector check.
 */

#include "unity.h"

#include "net_wifi_ap_password.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_known_vector(void)
{
    uint8_t mac[6] = { 0x00, 0x11, 0x22, 0xAA, 0xBB, 0xCC };
    char out[NET_WIFI_AP_PASSWORD_MAX];

    net_wifi_ap_password_from_mac(mac, out);

    TEST_ASSERT_EQUAL_STRING("dscaabbcc", out);
}

static void test_length_is_nine_characters(void)
{
    uint8_t mac[6] = { 1, 2, 3, 4, 5, 6 };
    char out[NET_WIFI_AP_PASSWORD_MAX];

    net_wifi_ap_password_from_mac(mac, out);

    TEST_ASSERT_EQUAL(9, strlen(out));
}

static void test_deterministic_for_same_mac(void)
{
    uint8_t mac[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02 };
    char out1[NET_WIFI_AP_PASSWORD_MAX];
    char out2[NET_WIFI_AP_PASSWORD_MAX];

    net_wifi_ap_password_from_mac(mac, out1);
    net_wifi_ap_password_from_mac(mac, out2);

    TEST_ASSERT_EQUAL_STRING(out1, out2);
}

static void test_different_macs_produce_different_passwords(void)
{
    uint8_t mac_a[6] = { 0, 0, 0, 0x01, 0x02, 0x03 };
    uint8_t mac_b[6] = { 0, 0, 0, 0x04, 0x05, 0x06 };
    char out_a[NET_WIFI_AP_PASSWORD_MAX];
    char out_b[NET_WIFI_AP_PASSWORD_MAX];

    net_wifi_ap_password_from_mac(mac_a, out_a);
    net_wifi_ap_password_from_mac(mac_b, out_b);

    TEST_ASSERT_NOT_EQUAL(0, strcmp(out_a, out_b));
}

static void test_charset_is_prefix_and_lowercase_hex(void)
{
    uint8_t mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    char out[NET_WIFI_AP_PASSWORD_MAX];

    net_wifi_ap_password_from_mac(mac, out);

    TEST_ASSERT_EQUAL_STRING("dscffffff", out);
    TEST_ASSERT_EQUAL('d', out[0]);
    TEST_ASSERT_EQUAL('s', out[1]);
    TEST_ASSERT_EQUAL('c', out[2]);
    for (size_t i = 3; i < strlen(out); i++) {
        char c = out[i];
        TEST_ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_vector);
    RUN_TEST(test_length_is_nine_characters);
    RUN_TEST(test_deterministic_for_same_mac);
    RUN_TEST(test_different_macs_produce_different_passwords);
    RUN_TEST(test_charset_is_prefix_and_lowercase_hex);
    return UNITY_END();
}
