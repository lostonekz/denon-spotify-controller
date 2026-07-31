/*
 * WiFi credential storage. docs/PLATFORM.md §3.2: namespace "secure", keys
 * `wifi.ssid`/`wifi.psk`, the same namespace Phase 4 will add Spotify's
 * secrets to. Pure -- takes an injected kv_store_if_t (already open on
 * "secure", opened by app_wiring via svc_storage) rather than depending on
 * svc_storage itself: the net layer has no svc edge (arch_rules.json).
 * Host-tested against test/host/fake_kv_store.c, same pattern as
 * svc_settings_migration.c.
 */
#ifndef NET_WIFI_CREDENTIALS_H
#define NET_WIFI_CREDENTIALS_H

#include <stdbool.h>

#include "kv_store_if.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_WIFI_SSID_MAX 33   /* IEEE 802.11 SSID: 32 bytes + NUL */
#define NET_WIFI_PSK_MAX  64   /* WPA2-PSK passphrase: 63 chars + NUL */

/* False if either key is missing or malformed (size mismatch) -- a
 * consistent "no credentials stored yet" signal, not a crash. */
bool net_wifi_credentials_load(kv_store_if_t *kv, char ssid_out[NET_WIFI_SSID_MAX],
                                  char psk_out[NET_WIFI_PSK_MAX]);

bool net_wifi_credentials_save(kv_store_if_t *kv, const char *ssid, const char *psk);

/* Idempotent: true (via commit()'s own result) regardless of whether
 * credentials existed to begin with -- the postcondition (no credentials
 * stored) holds either way. */
bool net_wifi_credentials_clear(kv_store_if_t *kv);

#ifdef __cplusplus
}
#endif

#endif /* NET_WIFI_CREDENTIALS_H */
