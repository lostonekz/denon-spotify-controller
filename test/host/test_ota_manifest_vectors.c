/*
 * Cross-language drift guard: for each vector in ota_manifest_vectors.csv
 * (also consumed by tools/ota_manifest_oracle.py, the independent Python
 * implementation), builds an ota_manifest_full_t from the raw fields and
 * asserts the C encoder (ota_manifest_canonical_bytes()) produces exactly
 * the same bytes the Python oracle already validated against the CSV.
 * A hand-mirrored binary encoding in two languages is exactly the kind of
 * thing that silently drifts apart without a shared vector table proving
 * otherwise -- see components/svc_ota/include/ota_manifest.h.
 */

#include "unity.h"

#include <string.h>

#include "ota_manifest.h"

#include "ota_manifest_vectors.inc"

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static void hex_decode(const char *hex, uint8_t *out, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        out[i] = (uint8_t)((hi << 4) | lo);
    }
}

static void hex_encode(const uint8_t *data, size_t len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = digits[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = digits[data[i] & 0xF];
    }
    out[len * 2] = '\0';
}

void setUp(void) {}
void tearDown(void) {}

static void test_all_vectors_match_python_oracle(void)
{
    for (size_t i = 0; i < OTA_MANIFEST_VECTORS_COUNT; i++) {
        const ota_manifest_vector_t *v = &OTA_MANIFEST_VECTORS[i];

        ota_manifest_full_t m = { 0 };
        strncpy(m.version, v->version, sizeof(m.version) - 1);
        m.build            = v->build;
        m.security_version = v->security_version;
        strncpy(m.url, v->url, sizeof(m.url) - 1);
        hex_decode(v->sha256_hex, m.sha256, OTA_SHA256_LEN);
        m.size = v->size;
        strncpy(m.key_id, v->key_id, sizeof(m.key_id) - 1);
        m.min_prev_build = v->min_prev_build;
        m.mandatory      = v->mandatory;
        strncpy(m.notes, v->notes, sizeof(m.notes) - 1);

        uint8_t out[OTA_MANIFEST_CANONICAL_MAX];
        size_t  out_len = 0;
        bool ok = ota_manifest_canonical_bytes(&m, out, sizeof(out), &out_len);

        TEST_ASSERT_TRUE_MESSAGE(ok, v->name);

        char got_hex[OTA_MANIFEST_CANONICAL_MAX * 2 + 1];
        hex_encode(out, out_len, got_hex);

        TEST_ASSERT_EQUAL_STRING_MESSAGE(v->expected_canonical_hex, got_hex, v->name);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_vectors_match_python_oracle);
    return UNITY_END();
}
