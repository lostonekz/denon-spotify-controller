/*
 * Deterministic SoftAP password. docs/SECURITY.md §5: "SoftAP stage --
 * WPA2 with a per-device password derived from the MAC and shown on
 * screen." (Phase 2: logged over UART instead of shown on screen, per the
 * user's Phase-2 scoping decision -- see net_wifi.h.)
 *
 * Deterministic (same MAC -> same password every boot) so re-entering
 * provisioning after a reset doesn't produce a different password a
 * previously-paired installer would need to re-read. Uses the low 3 MAC
 * octets (typically the most device-unique bytes) hex-encoded, prefixed,
 * for a 9-character result -- comfortably over WPA2's 8-character minimum.
 */
#ifndef NET_WIFI_AP_PASSWORD_H
#define NET_WIFI_AP_PASSWORD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_WIFI_AP_PASSWORD_MAX 10   /* "dsc" + 6 hex chars + NUL */

void net_wifi_ap_password_from_mac(const uint8_t mac[6], char out[NET_WIFI_AP_PASSWORD_MAX]);

#ifdef __cplusplus
}
#endif

#endif /* NET_WIFI_AP_PASSWORD_H */
