/*
 * Host test for ota_trust_anchors: table lookup by key_id only -- the
 * actual ECDSA verify (mbedTLS) is target-only, not covered here.
 */

#include "unity.h"

#include "ota_trust_anchors.h"

static const uint8_t FAKE_DER_A[] = { 0x01, 0x02, 0x03 };
static const uint8_t FAKE_DER_B[] = { 0x04, 0x05 };

static const ota_trust_anchor_t ANCHORS[] = {
    { "dsc-dev-2026-07", FAKE_DER_A, sizeof(FAKE_DER_A) },
    { "dsc-dev-2026-10", FAKE_DER_B, sizeof(FAKE_DER_B) },
};

static const ota_trust_anchors_t TRUST = {
    .anchors = ANCHORS,
    .count   = sizeof(ANCHORS) / sizeof(ANCHORS[0]),
};

void setUp(void) {}
void tearDown(void) {}

static void test_known_current_key_is_trusted(void)
{
    TEST_ASSERT_TRUE(ota_trust_anchors_is_trusted_key(&TRUST, "dsc-dev-2026-07"));
}

static void test_known_next_key_is_trusted(void)
{
    TEST_ASSERT_TRUE(ota_trust_anchors_is_trusted_key(&TRUST, "dsc-dev-2026-10"));
}

static void test_unknown_key_is_not_trusted(void)
{
    TEST_ASSERT_FALSE(ota_trust_anchors_is_trusted_key(&TRUST, "dsc-dev-1999-01"));
}

static void test_empty_key_is_not_trusted(void)
{
    TEST_ASSERT_FALSE(ota_trust_anchors_is_trusted_key(&TRUST, ""));
}

static void test_find_returns_matching_anchor(void)
{
    const ota_trust_anchor_t *a = ota_trust_anchors_find(&TRUST, "dsc-dev-2026-10");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_PTR(FAKE_DER_B, a->pubkey_der);
    TEST_ASSERT_EQUAL(sizeof(FAKE_DER_B), a->pubkey_der_len);
}

static void test_find_returns_null_for_unknown_key(void)
{
    TEST_ASSERT_NULL(ota_trust_anchors_find(&TRUST, "no-such-key"));
}

static void test_empty_table_trusts_nothing(void)
{
    ota_trust_anchors_t empty = { .anchors = NULL, .count = 0 };
    TEST_ASSERT_FALSE(ota_trust_anchors_is_trusted_key(&empty, "dsc-dev-2026-07"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_current_key_is_trusted);
    RUN_TEST(test_known_next_key_is_trusted);
    RUN_TEST(test_unknown_key_is_not_trusted);
    RUN_TEST(test_empty_key_is_not_trusted);
    RUN_TEST(test_find_returns_matching_anchor);
    RUN_TEST(test_find_returns_null_for_unknown_key);
    RUN_TEST(test_empty_table_trusts_nothing);
    return UNITY_END();
}
