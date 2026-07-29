/*
 * Host test for OTA admission policy.
 *
 * Covers the two acceptance-bar cases explicitly:
 *   - wrong-key image is rejected before boot
 *   - lower security_version image is rejected before boot
 *
 * Plus the ordering property that makes the second one meaningful: policy
 * checks must not run on an unverified manifest.
 */

#include <string.h>

#include "unity.h"

#include "ota_policy.h"

/* ---- fake verifier -------------------------------------------------- */

typedef struct {
    bool trust_key;
    bool manifest_sig_ok;
    bool image_sig_ok;

    /* Call log, so tests can assert ORDER, not just outcome. */
    int  n_trust;
    int  n_manifest_sig;
    int  n_image_sig;
} fake_ctx_t;

static bool fake_is_trusted(const char *key_id, void *ctx)
{
    (void)key_id;
    fake_ctx_t *f = (fake_ctx_t *)ctx;
    f->n_trust++;
    return f->trust_key;
}

static bool fake_manifest_sig(const ota_manifest_t *m, void *ctx)
{
    (void)m;
    fake_ctx_t *f = (fake_ctx_t *)ctx;
    f->n_manifest_sig++;
    return f->manifest_sig_ok;
}

static bool fake_image_sig(const uint8_t *sha, void *ctx)
{
    (void)sha;
    fake_ctx_t *f = (fake_ctx_t *)ctx;
    f->n_image_sig++;
    return f->image_sig_ok;
}

static fake_ctx_t g_fake;
static ota_verifier_t g_verifier;

static ota_manifest_t good_manifest(void)
{
    ota_manifest_t m;
    memset(&m, 0, sizeof(m));
    m.build = 142;
    m.security_version = 3;
    m.min_prev_build = 0;
    m.image_size = 2894336u;
    m.has_signature = true;
    strcpy(m.key_id, "dsc-dev-2026-07");
    for (int i = 0; i < OTA_SHA256_LEN; i++) {
        m.sha256[i] = (uint8_t)i;
    }
    return m;
}

static ota_device_state_t device_at(uint32_t build, uint32_t secver)
{
    ota_device_state_t d;
    d.running_build = build;
    d.running_security_version = secver;
    return d;
}

void setUp(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.trust_key = true;
    g_fake.manifest_sig_ok = true;
    g_fake.image_sig_ok = true;

    g_verifier.is_trusted_key = fake_is_trusted;
    g_verifier.verify_manifest_sig = fake_manifest_sig;
    g_verifier.verify_image_sig = fake_image_sig;
    g_verifier.ctx = &g_fake;
}

void tearDown(void) {}

/* ---- happy path ------------------------------------------------------ */

static void test_good_manifest_admitted(void)
{
    ota_manifest_t m = good_manifest();
    ota_device_state_t d = device_at(100, 3);
    TEST_ASSERT_EQUAL_INT(OTA_ADMIT_OK,
        ota_policy_admit_manifest(&m, &d, &g_verifier));
}

/* ---- ACCEPTANCE: wrong key rejected ---------------------------------- */

static void test_unknown_key_rejected(void)
{
    ota_manifest_t m = good_manifest();
    strcpy(m.key_id, "attacker-key-2026");
    ota_device_state_t d = device_at(100, 3);

    g_fake.trust_key = false;

    TEST_ASSERT_EQUAL_INT(OTA_REJECT_UNKNOWN_KEY,
        ota_policy_admit_manifest(&m, &d, &g_verifier));

    /* We must not even attempt signature verification against a key we do
     * not hold. */
    TEST_ASSERT_EQUAL_INT(0, g_fake.n_manifest_sig);
}

static void test_bad_manifest_signature_rejected(void)
{
    ota_manifest_t m = good_manifest();
    ota_device_state_t d = device_at(100, 3);

    g_fake.manifest_sig_ok = false;

    TEST_ASSERT_EQUAL_INT(OTA_REJECT_BAD_MANIFEST_SIG,
        ota_policy_admit_manifest(&m, &d, &g_verifier));
}

static void test_bad_image_signature_rejected_before_boot(void)
{
    ota_manifest_t m = good_manifest();
    uint8_t actual[OTA_SHA256_LEN];
    memcpy(actual, m.sha256, sizeof(actual));

    g_fake.image_sig_ok = false;

    TEST_ASSERT_EQUAL_INT(OTA_REJECT_BAD_IMAGE_SIG,
        ota_policy_admit_image(&m, m.image_size, actual, &g_verifier));
}

/* ---- ACCEPTANCE: lower security_version rejected --------------------- */

static void test_lower_security_version_rejected(void)
{
    ota_manifest_t m = good_manifest();
    m.security_version = 2;          /* device has already run 3 */
    m.build = 200;                   /* newer build, older security version */
    ota_device_state_t d = device_at(100, 3);

    TEST_ASSERT_EQUAL_INT(OTA_REJECT_ROLLBACK,
        ota_policy_admit_manifest(&m, &d, &g_verifier));
}

static void test_equal_security_version_allowed(void)
{
    /* Downgrade WITHIN a security version is legitimate -- it is how you back
     * out a bad feature release. Only security regressions are blocked. */
    ota_manifest_t m = good_manifest();
    m.security_version = 3;
    m.build = 150;
    ota_device_state_t d = device_at(100, 3);

    TEST_ASSERT_EQUAL_INT(OTA_ADMIT_OK,
        ota_policy_admit_manifest(&m, &d, &g_verifier));
}

/*
 * ORDERING PROPERTY, and the reason the rollback test above means anything:
 * security_version is attacker-controlled until the manifest signature is
 * verified. If policy ran first, a spoofed server could claim any version.
 */
static void test_signature_checked_before_version_policy(void)
{
    ota_manifest_t m = good_manifest();
    m.security_version = 0;   /* would trigger ROLLBACK if policy ran first */
    m.build = 1;              /* would trigger SAME_OR_OLDER_BUILD too */
    ota_device_state_t d = device_at(100, 3);

    g_fake.manifest_sig_ok = false;

    /* The signature failure must win. Any version-policy verdict here would
     * prove the checks are in the wrong order. */
    TEST_ASSERT_EQUAL_INT(OTA_REJECT_BAD_MANIFEST_SIG,
        ota_policy_admit_manifest(&m, &d, &g_verifier));
}

/* ---- integrity ------------------------------------------------------- */

static void test_truncated_download_rejected(void)
{
    ota_manifest_t m = good_manifest();
    uint8_t actual[OTA_SHA256_LEN];
    memcpy(actual, m.sha256, sizeof(actual));

    TEST_ASSERT_EQUAL_INT(OTA_REJECT_SIZE_MISMATCH,
        ota_policy_admit_image(&m, m.image_size - 1024u, actual, &g_verifier));
}

static void test_corrupt_content_rejected(void)
{
    ota_manifest_t m = good_manifest();
    uint8_t actual[OTA_SHA256_LEN];
    memcpy(actual, m.sha256, sizeof(actual));
    actual[31] ^= 0x01u;   /* single flipped bit */

    TEST_ASSERT_EQUAL_INT(OTA_REJECT_BAD_SHA256,
        ota_policy_admit_image(&m, m.image_size, actual, &g_verifier));
    TEST_ASSERT_EQUAL_INT(0, g_fake.n_image_sig);
}

/* ---- applicability --------------------------------------------------- */

static void test_same_build_is_noop(void)
{
    ota_manifest_t m = good_manifest();
    ota_device_state_t d = device_at(142, 3);
    TEST_ASSERT_EQUAL_INT(OTA_REJECT_SAME_OR_OLDER_BUILD,
        ota_policy_admit_manifest(&m, &d, &g_verifier));
}

static void test_stepping_stone_required(void)
{
    ota_manifest_t m = good_manifest();
    m.min_prev_build = 120;
    ota_device_state_t d = device_at(100, 3);
    TEST_ASSERT_EQUAL_INT(OTA_REJECT_PREV_BUILD_TOO_OLD,
        ota_policy_admit_manifest(&m, &d, &g_verifier));
}

static void test_malformed_manifest_rejected(void)
{
    ota_device_state_t d = device_at(100, 3);

    ota_manifest_t unsigned_m = good_manifest();
    unsigned_m.has_signature = false;
    TEST_ASSERT_EQUAL_INT(OTA_REJECT_MALFORMED_MANIFEST,
        ota_policy_admit_manifest(&unsigned_m, &d, &g_verifier));

    ota_manifest_t nokey = good_manifest();
    nokey.key_id[0] = '\0';
    TEST_ASSERT_EQUAL_INT(OTA_REJECT_MALFORMED_MANIFEST,
        ota_policy_admit_manifest(&nokey, &d, &g_verifier));

    ota_manifest_t zerosize = good_manifest();
    zerosize.image_size = 0;
    TEST_ASSERT_EQUAL_INT(OTA_REJECT_MALFORMED_MANIFEST,
        ota_policy_admit_manifest(&zerosize, &d, &g_verifier));

    TEST_ASSERT_EQUAL_INT(OTA_REJECT_MALFORMED_MANIFEST,
        ota_policy_admit_manifest(NULL, &d, &g_verifier));
}

/* --------------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_good_manifest_admitted);
    RUN_TEST(test_unknown_key_rejected);
    RUN_TEST(test_bad_manifest_signature_rejected);
    RUN_TEST(test_bad_image_signature_rejected_before_boot);
    RUN_TEST(test_lower_security_version_rejected);
    RUN_TEST(test_equal_security_version_allowed);
    RUN_TEST(test_signature_checked_before_version_policy);
    RUN_TEST(test_truncated_download_rejected);
    RUN_TEST(test_corrupt_content_rejected);
    RUN_TEST(test_same_build_is_noop);
    RUN_TEST(test_stepping_stone_required);
    RUN_TEST(test_malformed_manifest_rejected);
    return UNITY_END();
}
