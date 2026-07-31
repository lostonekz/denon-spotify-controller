/*
 * Host test for net_wifi_credentials: save/load/clear round-trip against
 * an in-memory kv_store_if_t, and safe truncation of oversized input.
 */

#include "unity.h"

#include "net_wifi_credentials.h"
#include "fake_kv_store.h"

#include <string.h>

static fake_kv_store_t g_backing;
static kv_store_if_t g_kv;

void setUp(void)
{
    fake_kv_store_init(&g_backing);
    g_kv = fake_kv_store_get(&g_backing);
}

void tearDown(void) {}

static void test_load_on_empty_store_returns_false(void)
{
    char ssid[NET_WIFI_SSID_MAX], psk[NET_WIFI_PSK_MAX];
    TEST_ASSERT_FALSE(net_wifi_credentials_load(&g_kv, ssid, psk));
}

static void test_save_then_load_round_trips(void)
{
    TEST_ASSERT_TRUE(net_wifi_credentials_save(&g_kv, "MyHomeNetwork", "hunter2hunter2"));

    char ssid[NET_WIFI_SSID_MAX], psk[NET_WIFI_PSK_MAX];
    TEST_ASSERT_TRUE(net_wifi_credentials_load(&g_kv, ssid, psk));
    TEST_ASSERT_EQUAL_STRING("MyHomeNetwork", ssid);
    TEST_ASSERT_EQUAL_STRING("hunter2hunter2", psk);
}

static void test_clear_removes_credentials(void)
{
    net_wifi_credentials_save(&g_kv, "MyHomeNetwork", "hunter2hunter2");
    TEST_ASSERT_TRUE(net_wifi_credentials_clear(&g_kv));

    char ssid[NET_WIFI_SSID_MAX], psk[NET_WIFI_PSK_MAX];
    TEST_ASSERT_FALSE(net_wifi_credentials_load(&g_kv, ssid, psk));
}

static void test_clear_on_never_saved_store_is_idempotent(void)
{
    TEST_ASSERT_TRUE(net_wifi_credentials_clear(&g_kv));
}

static void test_oversized_input_is_truncated_safely(void)
{
    char long_ssid[128];
    char long_psk[256];
    memset(long_ssid, 'a', sizeof(long_ssid) - 1);
    long_ssid[sizeof(long_ssid) - 1] = '\0';
    memset(long_psk, 'b', sizeof(long_psk) - 1);
    long_psk[sizeof(long_psk) - 1] = '\0';

    TEST_ASSERT_TRUE(net_wifi_credentials_save(&g_kv, long_ssid, long_psk));

    char ssid[NET_WIFI_SSID_MAX], psk[NET_WIFI_PSK_MAX];
    TEST_ASSERT_TRUE(net_wifi_credentials_load(&g_kv, ssid, psk));
    TEST_ASSERT_EQUAL(NET_WIFI_SSID_MAX - 1, strlen(ssid));
    TEST_ASSERT_EQUAL(NET_WIFI_PSK_MAX - 1, strlen(psk));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_on_empty_store_returns_false);
    RUN_TEST(test_save_then_load_round_trips);
    RUN_TEST(test_clear_removes_credentials);
    RUN_TEST(test_clear_on_never_saved_store_is_idempotent);
    RUN_TEST(test_oversized_input_is_truncated_safely);
    return UNITY_END();
}
