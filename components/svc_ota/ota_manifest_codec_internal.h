/*
 * Private helpers shared by ota_manifest_json.c: hex/base64 decoding, split
 * out purely to keep ota_manifest_json.c under tools/check_limits.py's
 * 400-line gate (same reasoning as net_wifi.c/net_wifi_actions.c's split
 * earlier in Phase 2). Not part of components/svc_ota/include -- nothing
 * outside this component should call these directly.
 */
#ifndef OTA_MANIFEST_CODEC_INTERNAL_H
#define OTA_MANIFEST_CODEC_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* hex must be exactly 64 chars (checked by the caller); out must hold 32 bytes. */
bool ota_manifest_hex_decode32(const char *hex, uint8_t *out);

/* Standard padded base64 (RFC 4648) only -- what Python's base64.b64encode
 * always produces. */
bool ota_manifest_base64_decode(const char *in, size_t in_len,
                                  uint8_t *out, size_t out_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANIFEST_CODEC_INTERNAL_H */
