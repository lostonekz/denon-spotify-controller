/*
 * OTA admission policy. See ota_policy.h for why this is separate.
 *
 * Ordering is deliberate and is itself part of the security property; the
 * host tests assert the ORDER, not just the outcome.
 */

#include "ota_policy.h"

#include <string.h>

ota_admit_t ota_policy_admit_manifest(const ota_manifest_t *m,
                                      const ota_device_state_t *dev,
                                      const ota_verifier_t *v)
{
    if (m == NULL || dev == NULL || v == NULL ||
        v->is_trusted_key == NULL || v->verify_manifest_sig == NULL) {
        return OTA_REJECT_MALFORMED_MANIFEST;
    }

    if (m->key_id[0] == '\0' || !m->has_signature || m->image_size == 0u) {
        return OTA_REJECT_MALFORMED_MANIFEST;
    }

    /* 1. Do we hold this trust anchor?
     *    Cheapest authenticity check, and it fails closed on an unknown key
     *    rather than attempting a verification we cannot perform. */
    if (!v->is_trusted_key(m->key_id, v->ctx)) {
        return OTA_REJECT_UNKNOWN_KEY;
    }

    /* 2. Is the manifest actually signed by it?
     *
     *    THIS MUST PRECEDE EVERY POLICY CHECK BELOW. All the version fields
     *    are attacker-controlled until this passes. Checking security_version
     *    against an unverified manifest would let a spoofed server claim any
     *    version it liked -- the check would run, and mean nothing. */
    if (!v->verify_manifest_sig(m, v->ctx)) {
        return OTA_REJECT_BAD_MANIFEST_SIG;
    }

    /* ---- everything below here is trusted data ---- */

    /* 3. Anti-rollback. Compared against the highest security_version ever
     *    run, not the currently-running one, so a downgrade cannot be laundered
     *    through an intermediate release. */
    if (m->security_version < dev->running_security_version) {
        return OTA_REJECT_ROLLBACK;
    }

    /* 4. Is it actually newer? Equal build is a no-op, not an error the user
     *    needs to see. */
    if (m->build <= dev->running_build) {
        return OTA_REJECT_SAME_OR_OLDER_BUILD;
    }

    /* 5. Does this release require a stepping stone? Used when a settings
     *    migration cannot safely skip versions. */
    if (m->min_prev_build != 0u && dev->running_build < m->min_prev_build) {
        return OTA_REJECT_PREV_BUILD_TOO_OLD;
    }

    return OTA_ADMIT_OK;
}

ota_admit_t ota_policy_admit_image(const ota_manifest_t *m,
                                   uint32_t actual_size,
                                   const uint8_t *actual_sha256,
                                   const ota_verifier_t *v)
{
    if (m == NULL || actual_sha256 == NULL || v == NULL ||
        v->verify_image_sig == NULL) {
        return OTA_REJECT_MALFORMED_MANIFEST;
    }

    /* 1. Size first: cheapest, and catches the truncated download that is the
     *    single most likely real failure. */
    if (actual_size != m->image_size) {
        return OTA_REJECT_SIZE_MISMATCH;
    }

    /* 2. Content matches what the (already authenticated) manifest promised.
     *    Constant-time compare: not because a timing attack is plausible here,
     *    but because using memcmp on a digest is the habit that eventually
     *    gets copied somewhere it does matter. */
    uint8_t diff = 0;
    for (size_t i = 0; i < OTA_SHA256_LEN; i++) {
        diff = (uint8_t)(diff | (m->sha256[i] ^ actual_sha256[i]));
    }
    if (diff != 0u) {
        return OTA_REJECT_BAD_SHA256;
    }

    /* 3. The image's own embedded signature, verified before the slot is
     *    marked bootable. Belt and braces with the manifest signature: the
     *    manifest could be authentic while pointing at a substituted binary. */
    if (!v->verify_image_sig(actual_sha256, v->ctx)) {
        return OTA_REJECT_BAD_IMAGE_SIG;
    }

    return OTA_ADMIT_OK;
}

const char *ota_admit_str(ota_admit_t a)
{
    switch (a) {
    case OTA_ADMIT_OK:                   return "OK";
    case OTA_REJECT_BAD_MANIFEST_SIG:    return "BAD_MANIFEST_SIG";
    case OTA_REJECT_UNKNOWN_KEY:         return "UNKNOWN_KEY";
    case OTA_REJECT_ROLLBACK:            return "ROLLBACK";
    case OTA_REJECT_BAD_SHA256:          return "BAD_SHA256";
    case OTA_REJECT_BAD_IMAGE_SIG:       return "BAD_IMAGE_SIG";
    case OTA_REJECT_SIZE_MISMATCH:       return "SIZE_MISMATCH";
    case OTA_REJECT_SAME_OR_OLDER_BUILD: return "SAME_OR_OLDER_BUILD";
    case OTA_REJECT_PREV_BUILD_TOO_OLD:  return "PREV_BUILD_TOO_OLD";
    case OTA_REJECT_MALFORMED_MANIFEST:  return "MALFORMED_MANIFEST";
    default:                             return "UNKNOWN";
    }
}
