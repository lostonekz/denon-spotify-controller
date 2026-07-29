/*
 * OTA admission policy — the decision layer, separated from the transport.
 *
 * Pure functions, no ESP-IDF, no network, no flash. Host-tested
 * (test/host/test_ota_policy.c).
 *
 * WHY SEPARATED: "should we install this image?" is the security-critical
 * decision in the whole product, and it is the one part of OTA that can be
 * exhaustively tested without a device, a server, or a flash write. Bundling
 * it into the download path would make it testable only end-to-end, which in
 * practice means barely tested at all.
 *
 * Signature VERIFICATION itself is delegated (mbedTLS on target, a fake in
 * tests) through an injected verifier. This file owns ORDERING and POLICY:
 * what is checked, in what order, and what a failure means.
 *
 * See docs/SECURITY.md §3 and docs/PLATFORM.md §2.3.
 */
#ifndef OTA_POLICY_H
#define OTA_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_ADMIT_OK = 0,

    /* Manifest signature failed or the named key is unknown.
     * This is the compromised-host / spoofed-server case (SECURITY.md T1). */
    OTA_REJECT_BAD_MANIFEST_SIG,
    OTA_REJECT_UNKNOWN_KEY,

    /* security_version lower than what we have already run.
     * Malicious or accidental downgrade to known-broken firmware (T2). */
    OTA_REJECT_ROLLBACK,

    /* Image content did not match the manifest. Corrupt or truncated
     * download -- the most likely real-world failure (T3). */
    OTA_REJECT_BAD_SHA256,
    OTA_REJECT_BAD_IMAGE_SIG,
    OTA_REJECT_SIZE_MISMATCH,

    /* Well-formed and authentic, but not applicable. */
    OTA_REJECT_SAME_OR_OLDER_BUILD,
    OTA_REJECT_PREV_BUILD_TOO_OLD,   /* manifest demands a stepping stone */

    OTA_REJECT_MALFORMED_MANIFEST,
} ota_admit_t;

#define OTA_SHA256_LEN 32
#define OTA_KEY_ID_LEN 32

typedef struct {
    uint32_t build;
    uint32_t security_version;
    uint32_t min_prev_build;
    uint32_t image_size;
    char     key_id[OTA_KEY_ID_LEN];
    uint8_t  sha256[OTA_SHA256_LEN];
    bool     has_signature;
} ota_manifest_t;

typedef struct {
    uint32_t running_build;
    uint32_t running_security_version;  /* highest EVER run, not current */
} ota_device_state_t;

/*
 * Injected verifier. Real implementation uses mbedTLS; tests use a fake that
 * can be told to fail, which is how the wrong-key path gets exercised
 * deterministically without generating real key material in CI.
 */
typedef struct {
    /* Return true iff key_id names a trust anchor we hold. Two anchors are
     * held at once (current + next) to make rotation possible -- see
     * docs/SECURITY.md §4. */
    bool (*is_trusted_key)(const char *key_id, void *ctx);

    /* Return true iff the manifest body signature is valid under key_id. */
    bool (*verify_manifest_sig)(const ota_manifest_t *m, void *ctx);

    /* Return true iff the downloaded image's embedded signature is valid. */
    bool (*verify_image_sig)(const uint8_t *sha256, void *ctx);

    void *ctx;
} ota_verifier_t;

/*
 * Stage 1: decide whether to DOWNLOAD, from the manifest alone.
 *
 * Cheap checks first, and authenticity before applicability: we do not want to
 * leak "which versions would you accept" to an unauthenticated manifest, and
 * we do not want to spend 3 MB of download on an image we would reject anyway.
 */
ota_admit_t ota_policy_admit_manifest(const ota_manifest_t *m,
                                      const ota_device_state_t *dev,
                                      const ota_verifier_t *v);

/*
 * Stage 2: decide whether to BOOT, after the image is downloaded.
 *
 * `actual_size` and `actual_sha256` are what we really received.
 * Must be called before the slot is marked bootable.
 */
ota_admit_t ota_policy_admit_image(const ota_manifest_t *m,
                                   uint32_t actual_size,
                                   const uint8_t *actual_sha256,
                                   const ota_verifier_t *v);

const char *ota_admit_str(ota_admit_t a);

#ifdef __cplusplus
}
#endif

#endif /* OTA_POLICY_H */
