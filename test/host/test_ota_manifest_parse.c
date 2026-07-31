/*
 * Host test for ota_manifest_json.c's strict parser. This is
 * attacker-controlled input (a compromised or spoofed OTA host), so almost
 * every test here is "this malformed variant must be rejected," not just
 * the one happy path -- see ota_manifest_json.c's own header comment for
 * the full adversarial-input list this proves.
 */

#include "unity.h"

#include "ota_manifest.h"

#include <string.h>

#define VALID_JSON \
    "{" \
    "\"version\":\"1.4.2\"," \
    "\"build\":142," \
    "\"security_version\":3," \
    "\"url\":\"https://ota.example.com/dsc-1.4.2.bin\"," \
    "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"," \
    "\"size\":2894336," \
    "\"sig\":\"QQ==\"," \
    "\"key_id\":\"dsc-dev-2026-07\"," \
    "\"min_prev_build\":100," \
    "\"mandatory\":false," \
    "\"notes\":\"Faster artwork transitions\"" \
    "}"

static ota_manifest_full_t g_m;

void setUp(void)   { memset(&g_m, 0, sizeof(g_m)); }
void tearDown(void) {}

static ota_admit_t parse(const char *json)
{
    return ota_manifest_parse_json(json, strlen(json), &g_m);
}

static void test_well_formed_manifest_parses(void)
{
    TEST_ASSERT_EQUAL(OTA_ADMIT_OK, parse(VALID_JSON));
    TEST_ASSERT_EQUAL_STRING("1.4.2", g_m.version);
    TEST_ASSERT_EQUAL_UINT32(142, g_m.build);
    TEST_ASSERT_EQUAL_UINT32(3, g_m.security_version);
    TEST_ASSERT_EQUAL_STRING("https://ota.example.com/dsc-1.4.2.bin", g_m.url);
    TEST_ASSERT_EQUAL_UINT32(2894336, g_m.size);
    TEST_ASSERT_EQUAL_STRING("dsc-dev-2026-07", g_m.key_id);
    TEST_ASSERT_EQUAL_UINT32(100, g_m.min_prev_build);
    TEST_ASSERT_FALSE(g_m.mandatory);
    TEST_ASSERT_EQUAL_STRING("Faster artwork transitions", g_m.notes);
    TEST_ASSERT_EQUAL(1, g_m.sig_len);
    TEST_ASSERT_EQUAL_UINT8('A', g_m.sig[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, g_m.sha256[0]);
    TEST_ASSERT_EQUAL_UINT8(0xef, g_m.sha256[31]);
}

static void test_optional_fields_default_when_absent(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";

    TEST_ASSERT_EQUAL(OTA_ADMIT_OK, parse(json));
    TEST_ASSERT_EQUAL_UINT32(0, g_m.min_prev_build);
    TEST_ASSERT_FALSE(g_m.mandatory);
    TEST_ASSERT_EQUAL_STRING("", g_m.notes);
}

static void test_duplicate_key_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"version\":\"1.0.1\",\"build\":1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_unknown_key_rejected(void)
{
    const char *json = "{\"version\":\"1.0.0\",\"bogus\":1}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_wrong_type_for_build_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":\"142\",\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_wrong_type_for_mandatory_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\",\"mandatory\":\"false\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_negative_build_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":-1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_fractional_build_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":1.5,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_leading_zero_build_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":007,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_build_overflowing_uint32_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":99999999999999,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_overlong_version_string_rejected(void)
{
    /* OTA_MANIFEST_VERSION_LEN is 16 -- 20 chars overflows it. */
    const char *json =
        "{\"version\":\"12345678901234567890\",\"build\":1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_sha256_too_short_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0123\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_sha256_non_hex_char_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"zz00000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_malformed_base64_sig_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"not-valid-base64!!\",\"key_id\":\"k\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_base64_sig_wrong_length_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ=\",\"key_id\":\"k\"}";   /* not a multiple of 4 */
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_embedded_nul_in_string_rejected(void)
{
    /* Built with strlen() rather than hardcoded byte counts so a typo here
     * can't silently under/over-run the buffer -- this deliberately
     * contains a raw NUL mid-string, so it cannot be a normal C string
     * literal built with plain concatenation. */
    char json[256];
    size_t n = 0;

    const char *part1 = "{\"version\":\"1.0";
    size_t len1 = strlen(part1);
    memcpy(&json[n], part1, len1);
    n += len1;

    json[n++] = '\0';   /* raw NUL byte inside the version string literal */

    const char *part2 =
        "0\",\"build\":1,\"security_version\":0,\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}";
    size_t len2 = strlen(part2);
    memcpy(&json[n], part2, len2);
    n += len2;

    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, ota_manifest_parse_json(json, n, &g_m));
}

static void test_unicode_escape_rejected(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\",\"notes\":\"caf\\u00e9\"}";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_raw_utf8_in_notes_is_accepted(void)
{
    const char *json =
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,"
        "\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\",\"notes\":\"caf\xc3\xa9\"}";
    TEST_ASSERT_EQUAL(OTA_ADMIT_OK, parse(json));
    TEST_ASSERT_EQUAL_STRING("caf\xc3\xa9", g_m.notes);
}

static void test_trailing_garbage_rejected(void)
{
    const char *json = VALID_JSON " garbage";
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(json));
}

static void test_empty_object_rejected(void)
{
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse("{}"));
}

static void test_not_an_object_rejected(void)
{
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse("[]"));
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse("\"just a string\""));
    TEST_ASSERT_EQUAL(OTA_REJECT_MALFORMED_MANIFEST, parse(""));
}

static void test_each_mandatory_field_missing_in_turn_is_rejected(void)
{
    const char *variants[] = {
        /* missing version */
        "{\"build\":1,\"security_version\":0,\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}",
        /* missing build */
        "{\"version\":\"1.0.0\",\"security_version\":0,\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}",
        /* missing security_version */
        "{\"version\":\"1.0.0\",\"build\":1,\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}",
        /* missing url */
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,"
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}",
        /* missing sha256 */
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,\"url\":\"https://x/y.bin\","
        "\"size\":1,\"sig\":\"QQ==\",\"key_id\":\"k\"}",
        /* missing size */
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"sig\":\"QQ==\",\"key_id\":\"k\"}",
        /* missing sig */
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"key_id\":\"k\"}",
        /* missing key_id */
        "{\"version\":\"1.0.0\",\"build\":1,\"security_version\":0,\"url\":\"https://x/y.bin\","
        "\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\","
        "\"size\":1,\"sig\":\"QQ==\"}",
    };
    for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
        TEST_ASSERT_EQUAL_MESSAGE(OTA_REJECT_MALFORMED_MANIFEST, parse(variants[i]),
                                    "expected rejection for a missing mandatory field");
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_well_formed_manifest_parses);
    RUN_TEST(test_optional_fields_default_when_absent);
    RUN_TEST(test_duplicate_key_rejected);
    RUN_TEST(test_unknown_key_rejected);
    RUN_TEST(test_wrong_type_for_build_rejected);
    RUN_TEST(test_wrong_type_for_mandatory_rejected);
    RUN_TEST(test_negative_build_rejected);
    RUN_TEST(test_fractional_build_rejected);
    RUN_TEST(test_leading_zero_build_rejected);
    RUN_TEST(test_build_overflowing_uint32_rejected);
    RUN_TEST(test_overlong_version_string_rejected);
    RUN_TEST(test_sha256_too_short_rejected);
    RUN_TEST(test_sha256_non_hex_char_rejected);
    RUN_TEST(test_malformed_base64_sig_rejected);
    RUN_TEST(test_base64_sig_wrong_length_rejected);
    RUN_TEST(test_embedded_nul_in_string_rejected);
    RUN_TEST(test_unicode_escape_rejected);
    RUN_TEST(test_raw_utf8_in_notes_is_accepted);
    RUN_TEST(test_trailing_garbage_rejected);
    RUN_TEST(test_empty_object_rejected);
    RUN_TEST(test_not_an_object_rejected);
    RUN_TEST(test_each_mandatory_field_missing_in_turn_is_rejected);
    return UNITY_END();
}
