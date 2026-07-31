/*
 * The full OTA manifest, as fetched over the network -- every field in
 * docs/PLATFORM.md §2.3's example, not just the subset `ota_policy_t`
 * needs for its admission decision. Kept as a separate, richer type
 * (`ota_manifest_full_t`) rather than growing `ota_policy.h`'s own
 * `ota_manifest_t`, which is already built, host-tested (12 tests), and
 * must not change shape out from under those tests. A projection function
 * (added alongside the real parser, Tier 4) narrows this down to
 * `ota_manifest_t` for the actual `ota_policy_admit_manifest()` call.
 *
 * `ota_manifest_canonical_bytes()` is the one thing needed now, ahead of
 * the real parser: the fixed-order binary encoding that gets ECDSA-signed
 * (docs/SECURITY.md §3's manifest signature). Defined once, here, and
 * mirrored in `tools/ota_manifest_oracle.py` -- the two are checked against
 * shared vectors (`test/host/ota_manifest_vectors.csv`) so the encoding
 * cannot silently drift between the Python signer and the C verifier, the
 * same reasoning `tools/denon_mv_oracle.py` already applies to the Denon MV
 * codec.
 *
 * Signed fields exclude `sig` itself (obviously) but include everything
 * else, `notes` included -- simpler and harder to get wrong than deciding
 * per-field whether omitting it from the signature is safe.
 */
#ifndef OTA_MANIFEST_H
#define OTA_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_policy.h"   /* OTA_SHA256_LEN, OTA_KEY_ID_LEN */

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_MANIFEST_VERSION_LEN 16
#define OTA_MANIFEST_URL_LEN     128
#define OTA_MANIFEST_NOTES_LEN   64
#define OTA_MANIFEST_SIG_MAX     80   /* ECDSA P-256 DER signature, generously bounded */

/* Upper bound on ota_manifest_canonical_bytes()'s output: 1+16 (version) +
 * 4 (build) + 4 (security_version) + 2+128 (url) + 32 (sha256) + 4 (size) +
 * 1+32 (key_id) + 4 (min_prev_build) + 1 (mandatory) + 2+64 (notes). */
#define OTA_MANIFEST_CANONICAL_MAX 320

typedef struct {
    char     version[OTA_MANIFEST_VERSION_LEN];
    uint32_t build;
    uint32_t security_version;
    char     url[OTA_MANIFEST_URL_LEN];
    uint8_t  sha256[OTA_SHA256_LEN];
    uint32_t size;
    char     key_id[OTA_KEY_ID_LEN];
    uint32_t min_prev_build;
    bool     mandatory;
    char     notes[OTA_MANIFEST_NOTES_LEN];
    uint8_t  sig[OTA_MANIFEST_SIG_MAX];
    size_t   sig_len;
} ota_manifest_full_t;

/*
 * Encodes every field of `m` except `sig`/`sig_len` into `out` as:
 *   [1B version_len][version] [4B build BE] [4B security_version BE]
 *   [2B url_len BE][url] [32B sha256] [4B size BE]
 *   [1B key_id_len][key_id] [4B min_prev_build BE] [1B mandatory]
 *   [2B notes_len BE][notes]
 * Big-endian, length-prefixed strings (not NUL-terminated in the encoding)
 * so no field's content can ever shift a later field's boundary.
 *
 * False (out_len untouched) if out_cap is insufficient -- see
 * OTA_MANIFEST_CANONICAL_MAX for a safe capacity.
 */
bool ota_manifest_canonical_bytes(const ota_manifest_full_t *m,
                                    uint8_t *out, size_t out_cap, size_t *out_len);

/*
 * Parses raw manifest bytes fetched over HTTPS into `out`. Attacker-
 * controlled input (a compromised or spoofed OTA host), so this is
 * deliberately strict rather than permissive -- see ota_manifest_json.c's
 * own header comment for the exact adversarial-input posture (duplicate/
 * unknown keys, wrong types, out-of-range integers, overlong strings,
 * malformed hex/base64, embedded control bytes, missing mandatory fields
 * are all OTA_REJECT_MALFORMED_MANIFEST, never a best-effort partial
 * parse). Hand-rolled, not cJSON, per MEMORY.md §5 -- the schema is a
 * single flat object with a fixed, known key set, which a general-purpose
 * JSON library is the wrong size of tool for.
 *
 * Only ever returns OTA_ADMIT_OK or OTA_REJECT_MALFORMED_MANIFEST -- it is
 * a syntax/schema check, not the admission policy itself (ota_policy.h
 * owns authenticity/rollback/applicability, run afterward via
 * ota_manifest_project() below).
 */
ota_admit_t ota_manifest_parse_json(const char *json, size_t json_len, ota_manifest_full_t *out);

/* Narrows the full, richer manifest down to the subset ota_policy.h's
 * admission decision actually needs -- kept separate from parsing itself
 * so ota_policy_admit_manifest()'s existing 12 host tests never had to
 * change shape when the real parser landed (see this header's own top
 * comment). Pure field copy; cannot fail. */
void ota_manifest_project(const ota_manifest_full_t *full, ota_manifest_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANIFEST_H */
