/*
 * Host test for spotify_credentials: save/load/clear round-trip for the
 * refresh token + paired-at timestamp, client_id/redirect_uri, and the
 * pinned AVR device id/name -- against an in-memory kv_store_if_t, same
 * pattern as test_net_wifi_credentials.c.
 */

#include "unity.h"

#include "spotify_credentials.h"
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
    spotify_session_credentials_t creds;
    TEST_ASSERT_FALSE(spotify_credentials_load(&g_kv, &creds));
}

static void test_save_then_load_round_trips(void)
{
    spotify_session_credentials_t in = { 0 };
    strncpy(in.refresh_token, "AQD3exampletoken", sizeof(in.refresh_token) - 1);
    in.paired_at_epoch_s = 1780000000;

    TEST_ASSERT_TRUE(spotify_credentials_save(&g_kv, &in));

    spotify_session_credentials_t out;
    TEST_ASSERT_TRUE(spotify_credentials_load(&g_kv, &out));
    TEST_ASSERT_EQUAL_STRING("AQD3exampletoken", out.refresh_token);
    TEST_ASSERT_EQUAL_INT64(1780000000, out.paired_at_epoch_s);
}

static void test_clear_removes_credentials(void)
{
    spotify_session_credentials_t in = { 0 };
    strncpy(in.refresh_token, "AQD3exampletoken", sizeof(in.refresh_token) - 1);
    spotify_credentials_save(&g_kv, &in);

    TEST_ASSERT_TRUE(spotify_credentials_clear(&g_kv));

    spotify_session_credentials_t out;
    TEST_ASSERT_FALSE(spotify_credentials_load(&g_kv, &out));
}

static void test_clear_on_never_saved_store_is_idempotent(void)
{
    TEST_ASSERT_TRUE(spotify_credentials_clear(&g_kv));
}

static void test_client_id_round_trips(void)
{
    char out[SPOTIFY_CLIENT_ID_MAX];

    TEST_ASSERT_FALSE(spotify_client_id_load(&g_kv, out));
    TEST_ASSERT_TRUE(spotify_client_id_save(&g_kv, "abc123def456"));

    TEST_ASSERT_TRUE(spotify_client_id_load(&g_kv, out));
    TEST_ASSERT_EQUAL_STRING("abc123def456", out);
}

static void test_redirect_uri_round_trips(void)
{
    TEST_ASSERT_TRUE(spotify_redirect_uri_save(&g_kv, "http://127.0.0.1:8888/callback"));

    char out[SPOTIFY_REDIRECT_URI_MAX];
    TEST_ASSERT_TRUE(spotify_redirect_uri_load(&g_kv, out));
    TEST_ASSERT_EQUAL_STRING("http://127.0.0.1:8888/callback", out);
}

static void test_avr_pin_round_trips(void)
{
    spotify_avr_pin_t in = { 0 };
    strncpy(in.avr_device_id, "abcdef0123456789", sizeof(in.avr_device_id) - 1);
    strncpy(in.avr_name, "Denon AVR-X3800H", sizeof(in.avr_name) - 1);

    TEST_ASSERT_TRUE(spotify_avr_pin_save(&g_kv, &in));

    spotify_avr_pin_t out;
    TEST_ASSERT_TRUE(spotify_avr_pin_load(&g_kv, &out));
    TEST_ASSERT_EQUAL_STRING("abcdef0123456789", out.avr_device_id);
    TEST_ASSERT_EQUAL_STRING("Denon AVR-X3800H", out.avr_name);
}

static void test_avr_pin_clear_removes_pin(void)
{
    spotify_avr_pin_t in = { 0 };
    strncpy(in.avr_device_id, "abcdef0123456789", sizeof(in.avr_device_id) - 1);
    spotify_avr_pin_save(&g_kv, &in);

    TEST_ASSERT_TRUE(spotify_avr_pin_clear(&g_kv));

    spotify_avr_pin_t out;
    TEST_ASSERT_FALSE(spotify_avr_pin_load(&g_kv, &out));
}

static void test_avr_pin_load_fails_if_only_one_key_present(void)
{
    /* Simulates a malformed/partial write -- id alone is not a valid pin. */
    char id_buf[SPOTIFY_AVR_DEVICE_ID_MAX] = { 0 };
    strncpy(id_buf, "abcdef0123456789", sizeof(id_buf) - 1);
    g_kv.set_blob(g_kv.ctx, "sp.avr_device", id_buf, sizeof(id_buf));
    g_kv.commit(g_kv.ctx);

    spotify_avr_pin_t out;
    TEST_ASSERT_FALSE(spotify_avr_pin_load(&g_kv, &out));
}

static void test_oversized_refresh_token_is_truncated_safely(void)
{
    spotify_session_credentials_t in = { 0 };
    char long_token[500];
    memset(long_token, 'x', sizeof(long_token) - 1);
    long_token[sizeof(long_token) - 1] = '\0';
    strncpy(in.refresh_token, long_token, sizeof(in.refresh_token) - 1);

    TEST_ASSERT_TRUE(spotify_credentials_save(&g_kv, &in));

    spotify_session_credentials_t out;
    TEST_ASSERT_TRUE(spotify_credentials_load(&g_kv, &out));
    TEST_ASSERT_EQUAL(SPOTIFY_REFRESH_TOKEN_MAX - 1, strlen(out.refresh_token));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_on_empty_store_returns_false);
    RUN_TEST(test_save_then_load_round_trips);
    RUN_TEST(test_clear_removes_credentials);
    RUN_TEST(test_clear_on_never_saved_store_is_idempotent);
    RUN_TEST(test_client_id_round_trips);
    RUN_TEST(test_redirect_uri_round_trips);
    RUN_TEST(test_avr_pin_round_trips);
    RUN_TEST(test_avr_pin_clear_removes_pin);
    RUN_TEST(test_avr_pin_load_fails_if_only_one_key_present);
    RUN_TEST(test_oversized_refresh_token_is_truncated_safely);
    return UNITY_END();
}
