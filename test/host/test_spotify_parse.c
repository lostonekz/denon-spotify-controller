/*
 * Host test for spotify_parse: fixture-driven against captured-shape JSON
 * (test/host/fixtures/spotify_*.json -- built from Spotify's own
 * publicly-documented example response shapes, since no real captured
 * payload exists yet; Tier 6 supplements/corrects these once hardware
 * verification is possible, per R5's design intent). Also covers the
 * 204-empty-body case and adversarial/malformed input.
 *
 * FIXTURES_DIR is injected by CMake (see test/host/CMakeLists.txt) as the
 * absolute path to test/host/fixtures/.
 */

#include "unity.h"

#include "spotify_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FIXTURES_DIR
#error "FIXTURES_DIR must be defined by CMake"
#endif

void setUp(void) {}
void tearDown(void) {}

/* Loads a fixture file fully into a heap buffer; the caller owns and frees
 * it. Test-only helper -- production code never does unbounded file I/O
 * like this. */
static char *load_fixture(const char *filename, size_t *out_len)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FIXTURES_DIR, filename);

    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    if (f == NULL) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    TEST_ASSERT_TRUE(size >= 0);

    char *buf = malloc((size_t)size + 1);
    TEST_ASSERT_NOT_NULL(buf);
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';

    if (out_len != NULL) {
        *out_len = read;
    }
    return buf;
}

static void test_playing_via_avr(void)
{
    size_t len;
    char *json = load_fixture("spotify_player_avr.json", &len);

    playback_state_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_OK, spotify_parse_player_state(json, len, &out));

    TEST_ASSERT_EQUAL_STRING("Mr. Brightside", out.title);
    TEST_ASSERT_EQUAL_STRING("The Killers", out.artist);
    TEST_ASSERT_EQUAL_STRING("Hot Fuss", out.album);
    TEST_ASSERT_EQUAL_STRING("3n3Ppam7vgaVa1iaRUc9Lp", out.track_id);
    TEST_ASSERT_EQUAL_STRING("https://i.scdn.co/image/ab67616d0000b273example640", out.artwork_url_640);
    TEST_ASSERT_EQUAL_STRING("5fbb3ba6aa454b5534c4ba43a8c7e6f0d3a1b2c3", out.device_id);
    TEST_ASSERT_EQUAL_STRING("Denon AVR-X3800H", out.device_name);
    TEST_ASSERT_TRUE(out.device_supports_volume);
    TEST_ASSERT_FALSE(out.device_is_restricted);
    TEST_ASSERT_EQUAL_UINT32(64213, out.position_ms);
    TEST_ASSERT_EQUAL_UINT32(222973, out.duration_ms);
    TEST_ASSERT_TRUE(out.is_playing);
    TEST_ASSERT_FALSE(out.shuffle);
    TEST_ASSERT_EQUAL(SPOTIFY_REPEAT_OFF, out.repeat_mode);

    free(json);
}

static void test_playing_via_other_device(void)
{
    size_t len;
    char *json = load_fixture("spotify_player_other_device.json", &len);

    playback_state_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_OK, spotify_parse_player_state(json, len, &out));

    TEST_ASSERT_EQUAL_STRING("Blinding Lights", out.title);
    TEST_ASSERT_EQUAL_STRING("Gabriel's Phone", out.device_name);
    TEST_ASSERT_TRUE(out.shuffle);
    TEST_ASSERT_EQUAL(SPOTIFY_REPEAT_CONTEXT, out.repeat_mode);

    free(json);
}

static void test_paused(void)
{
    size_t len;
    char *json = load_fixture("spotify_player_paused.json", &len);

    playback_state_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_OK, spotify_parse_player_state(json, len, &out));

    TEST_ASSERT_FALSE(out.is_playing);
    TEST_ASSERT_EQUAL(SPOTIFY_REPEAT_TRACK, out.repeat_mode);
    TEST_ASSERT_EQUAL_UINT32(128000, out.position_ms);

    free(json);
}

static void test_204_no_active_device_is_not_an_error(void)
{
    playback_state_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_NO_ACTIVE_DEVICE, spotify_parse_player_state("", 0, &out));
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_NO_ACTIVE_DEVICE, spotify_parse_player_state(NULL, 0, &out));
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_NO_ACTIVE_DEVICE, spotify_parse_player_state("   \n", 4, &out));
}

static void test_device_list(void)
{
    size_t len;
    char *json = load_fixture("spotify_devices.json", &len);

    spotify_device_list_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_OK, spotify_parse_device_list(json, len, &out));

    TEST_ASSERT_EQUAL(2, out.count);
    TEST_ASSERT_EQUAL_STRING("Denon AVR-X3800H", out.devices[0].device_name);
    TEST_ASSERT_EQUAL_STRING("AVR", out.devices[0].device_type);
    TEST_ASSERT_TRUE(out.devices[0].supports_volume);
    TEST_ASSERT_EQUAL_STRING("Gabriel's Phone", out.devices[1].device_name);

    free(json);
}

static void test_token_exchange_ok(void)
{
    size_t len;
    char *json = load_fixture("spotify_token_ok.json", &len);

    spotify_token_response_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_TOKEN_RESULT_OK, spotify_parse_token_response(json, len, &out));

    TEST_ASSERT_EQUAL_STRING("BQD4exampleaccesstoken1234567890abcdefg", out.access_token);
    TEST_ASSERT_EQUAL_STRING("AQD3examplerefreshtoken0987654321zyxwvut", out.refresh_token);
    TEST_ASSERT_EQUAL_UINT32(3600, out.expires_in_s);

    free(json);
}

static void test_token_invalid_grant(void)
{
    size_t len;
    char *json = load_fixture("spotify_token_invalid_grant.json", &len);

    spotify_token_response_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_TOKEN_RESULT_INVALID_GRANT, spotify_parse_token_response(json, len, &out));

    free(json);
}

static void test_token_other_error(void)
{
    const char *json = "{\"error\":\"server_error\"}";
    spotify_token_response_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_TOKEN_RESULT_OTHER_ERROR,
        spotify_parse_token_response(json, strlen(json), &out));
}

/* --- Adversarial input: R5's own reason for confining Spotify's JSON
 * shape knowledge to this one file -- a hostile/malformed response must
 * never crash, only fail cleanly. --- */

static void test_truncated_json_is_malformed(void)
{
    const char *json = "{\"device\": {\"id\": \"abc";
    playback_state_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_MALFORMED, spotify_parse_player_state(json, strlen(json), &out));
}

static void test_missing_item_leaves_track_fields_untouched(void)
{
    const char *json = "{\"is_playing\": true, \"progress_ms\": 500}";
    playback_state_t out = { 0 };
    strncpy(out.title, "previous title", sizeof(out.title) - 1);

    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_OK, spotify_parse_player_state(json, strlen(json), &out));
    TEST_ASSERT_EQUAL_STRING("previous title", out.title);   /* untouched, not blanked -- R5 tolerance */
    TEST_ASSERT_TRUE(out.is_playing);
}

static void test_unexpected_nesting_does_not_crash(void)
{
    const char *json = "{\"device\": \"not an object\", \"item\": [1,2,3]}";
    playback_state_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_OK, spotify_parse_player_state(json, strlen(json), &out));
}

static void test_oversized_string_is_truncated_not_overflowed(void)
{
    char json[600];
    char long_name[500];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    snprintf(json, sizeof(json), "{\"device\": {\"name\": \"%s\"}}", long_name);

    playback_state_t out = { 0 };
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_OK, spotify_parse_player_state(json, strlen(json), &out));
    TEST_ASSERT_EQUAL(PLAYBACK_DEVICE_NAME_MAX - 1, strlen(out.device_name));
}

static void test_null_out_pointer_rejected(void)
{
    TEST_ASSERT_EQUAL(SPOTIFY_PARSE_MALFORMED, spotify_parse_player_state("{}", 2, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_playing_via_avr);
    RUN_TEST(test_playing_via_other_device);
    RUN_TEST(test_paused);
    RUN_TEST(test_204_no_active_device_is_not_an_error);
    RUN_TEST(test_device_list);
    RUN_TEST(test_token_exchange_ok);
    RUN_TEST(test_token_invalid_grant);
    RUN_TEST(test_token_other_error);
    RUN_TEST(test_truncated_json_is_malformed);
    RUN_TEST(test_missing_item_leaves_track_fields_untouched);
    RUN_TEST(test_unexpected_nesting_does_not_crash);
    RUN_TEST(test_oversized_string_is_truncated_not_overflowed);
    RUN_TEST(test_null_out_pointer_rejected);
    return UNITY_END();
}
