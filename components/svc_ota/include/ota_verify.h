/*
 * Real implementations of ota_verifier_t's three callbacks (ota_policy.h),
 * target-only (mbedTLS + esp_image_format). This is what makes ota_policy.c
 * and ota_fsm.c's decisions actually mean something on hardware, rather
 * than only in a host test against a fake verifier.
 *
 * ota_verifier_t.verify_manifest_sig() takes the NARROW ota_manifest_t
 * (ota_policy.h, Phase 0, already host-tested against that exact
 * signature) -- but ECDSA verification needs the FULL manifest's canonical
 * bytes (version/url/notes included), which the narrow type does not
 * carry. Changing ota_policy.h's callback signature now would break its
 * existing 12 host tests over a Phase 0 contract this phase deliberately
 * leaves alone (see components/svc_ota/include/ota_manifest.h's own top
 * comment). The fix: ota_verify_ctx_t.pending_full_manifest, set by the
 * caller (svc_ota_download.c) immediately before each
 * ota_policy_admit_manifest() call -- ota_verify_manifest_sig() reads the
 * canonical bytes from there, not from its own `m` parameter, whose
 * fields it never touches.
 */
#ifndef OTA_VERIFY_H
#define OTA_VERIFY_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_partition.h"

#include "ota_manifest.h"
#include "ota_policy.h"
#include "ota_trust_anchors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ota_trust_anchors_t          trust;

    /* Set by svc_ota_download.c right before ota_policy_admit_manifest();
     * NULL otherwise. */
    const ota_manifest_full_t   *pending_full_manifest;

    /* Set by svc_ota_download.c right before ota_policy_admit_image() --
     * the partition esp_ota_write() just streamed the image into. */
    const esp_partition_t       *target_partition;
} ota_verify_ctx_t;

/* ota_verifier_t.is_trusted_key */
bool ota_verify_is_trusted_key(const char *key_id, void *ctx);

/* ota_verifier_t.verify_manifest_sig -- ECDSA P-256 over
 * ota_manifest_canonical_bytes(ctx->pending_full_manifest), mbedTLS. */
bool ota_verify_manifest_sig(const ota_manifest_t *m, void *ctx);

/* ota_verifier_t.verify_image_sig -- an independent esp_image_verify() of
 * ctx->target_partition, defense in depth over esp_ota_end()'s own
 * RSA-3072 enforcement (already active under
 * CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y, docs/SECURITY.md §3). Does
 * not use its own `sha256` parameter -- esp_image_verify() re-derives and
 * checks the image's digest and signature directly from flash. */
bool ota_verify_image_sig(const uint8_t *sha256, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* OTA_VERIFY_H */
