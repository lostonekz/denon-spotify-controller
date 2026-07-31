/*
 * Persisted Spotify config: the refresh token and everything alongside it.
 * docs/PLATFORM.md §3.2 illustrates the secure-namespace keys as
 * `spotify.refresh_token`/`spotify.client_id` -- both exceed KV_KEY_MAX (15
 * bytes; `spotify.refresh_token` is 21 chars), so the real keys below
 * follow `wifi.ssid`/`wifi.psk`'s own short-key convention instead. Pure --
 * takes an injected kv_store_if_t (already open on "secure", opened by
 * app_wiring via svc_storage), exactly like net_wifi_credentials.h. Host-
 * tested against test/host/fake_kv_store.c.
 *
 * "Credentials" now covers more than the refresh token: the pinned AVR
 * device id/name (spotify_avr_match.h) and the paired-at timestamp
 * (spotify_reauth_watch.h) live here too -- kept as one small, cohesive
 * kv_store_if_t-backed module rather than three, since none of the three
 * warrants its own file at this size.
 *
 * NOTE on storage protection: `ctx->secure_kv` is opened with
 * KV_NS_ENCRYPTED, but that is a documented no-op today (dev-profile
 * plaintext, see svc_storage.h and adr/0006-staged-security.md) until a
 * real nvs_flash_secure_init() production path exists. The refresh token
 * persisted here is the first genuinely high-value secret this codebase
 * stores -- accepted plainly, not silently, same staged posture WiFi's PSK
 * already uses. `spotify_credentials_clear()` is the way to actually erase
 * it (spotify unpair / factory reset).
 */
#ifndef SPOTIFY_CREDENTIALS_H
#define SPOTIFY_CREDENTIALS_H

#include <stdbool.h>
#include <stdint.h>

#include "kv_store_if.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTIFY_REFRESH_TOKEN_MAX 300   /* observed Spotify refresh tokens run ~150-260 chars */
#define SPOTIFY_CLIENT_ID_MAX     33    /* Spotify client_id: 32 hex chars + NUL */
#define SPOTIFY_REDIRECT_URI_MAX  128
#define SPOTIFY_AVR_DEVICE_ID_MAX 48
#define SPOTIFY_AVR_NAME_MAX      32

typedef struct {
    char    refresh_token[SPOTIFY_REFRESH_TOKEN_MAX];
    int64_t paired_at_epoch_s;   /* Unix epoch seconds, time(NULL) at first EXCHANGE_OK */
} spotify_session_credentials_t;

typedef struct {
    char avr_device_id[SPOTIFY_AVR_DEVICE_ID_MAX];
    char avr_name[SPOTIFY_AVR_NAME_MAX];
} spotify_avr_pin_t;

/* False if the refresh token / paired-at pair is missing or malformed
 * (size mismatch) -- a consistent "not paired yet" signal, not a crash. */
bool spotify_credentials_load(kv_store_if_t *kv, spotify_session_credentials_t *out);
bool spotify_credentials_save(kv_store_if_t *kv, const spotify_session_credentials_t *in);
/* Idempotent, same reasoning as net_wifi_credentials_clear(). */
bool spotify_credentials_clear(kv_store_if_t *kv);

/* client_id/redirect_uri: not confidential under PKCE (no client_secret
 * involved), unlike the refresh token -- still persisted here per
 * PLATFORM.md §3.2's stated location, with a Kconfig-seeded default
 * app_wiring applies only when nothing is stored yet (spotify_service.h's
 * top comment). */
bool spotify_client_id_load(kv_store_if_t *kv, char out[SPOTIFY_CLIENT_ID_MAX]);
bool spotify_client_id_save(kv_store_if_t *kv, const char *client_id);
bool spotify_redirect_uri_load(kv_store_if_t *kv, char out[SPOTIFY_REDIRECT_URI_MAX]);
bool spotify_redirect_uri_save(kv_store_if_t *kv, const char *redirect_uri);

/* The pinned AVR device id/name (spotify_avr_match.h) -- set together,
 * atomically, by `spotify set-avr-device`; survives an unpair/re-pair cycle
 * since it identifies a piece of hardware, not a session. */
bool spotify_avr_pin_load(kv_store_if_t *kv, spotify_avr_pin_t *out);
bool spotify_avr_pin_save(kv_store_if_t *kv, const spotify_avr_pin_t *in);
bool spotify_avr_pin_clear(kv_store_if_t *kv);

#ifdef __cplusplus
}
#endif

#endif /* SPOTIFY_CREDENTIALS_H */
