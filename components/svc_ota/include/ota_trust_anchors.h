/*
 * The compiled-in ECDSA P-256 manifest-signing trust anchors.
 * docs/SECURITY.md §4: "the firmware holds two trust anchors: current and
 * next. Either verifies" -- rotation without a flag day, so a lost or
 * compromised key is recoverable as long as one of the two still is not.
 *
 * Pure table lookup by key_id, host-tested with fake anchors -- the actual
 * key bytes (dev or release) are generated data, not logic, and live in
 * ota_trust_anchor_dev.c (tools/gen_trust_anchor.py, Tier 5) as a
 * `static const ota_trust_anchor_t[]` this same type describes.
 *
 * Each anchor's pubkey is a DER-encoded SubjectPublicKeyInfo (what
 * mbedtls_pk_parse_public_key() consumes directly, no manual EC-point
 * unpacking needed) -- that parsing, and the actual ECDSA verify, are
 * target-only (ota_verify.c, Tier 4f): this file only owns "which key_id do
 * we hold at all," not "is this signature valid under it."
 */
#ifndef OTA_TRUST_ANCHORS_H
#define OTA_TRUST_ANCHORS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char     *key_id;       /* e.g. "dsc-dev-2026-07", matches manifest.json's key_id */
    const uint8_t  *pubkey_der;   /* DER SubjectPublicKeyInfo, EC P-256 */
    size_t           pubkey_der_len;
} ota_trust_anchor_t;

typedef struct {
    const ota_trust_anchor_t *anchors;   /* caller-owned, count entries */
    size_t                     count;
} ota_trust_anchors_t;

/* True iff key_id matches one of the held anchors (current or next, or any
 * other count the caller wires up). Byte-exact, case-sensitive match. */
bool ota_trust_anchors_is_trusted_key(const ota_trust_anchors_t *t, const char *key_id);

/* NULL if key_id names no held anchor -- the caller (ota_verify.c) still
 * has to check that before dereferencing, same "unknown key" outcome
 * ota_policy_admit_manifest() already surfaces as OTA_REJECT_UNKNOWN_KEY. */
const ota_trust_anchor_t *ota_trust_anchors_find(const ota_trust_anchors_t *t, const char *key_id);

#ifdef __cplusplus
}
#endif

#endif /* OTA_TRUST_ANCHORS_H */
