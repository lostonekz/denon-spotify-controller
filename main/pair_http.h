/*
 * The Spotify pairing page, served from this device over the LAN.
 * docs/adr/0011 supersedes adr/0009 for this.
 *
 * Flow: the panel shows a QR for http://<ip>:<port>/pair -> the phone opens
 * that page -> the page offers the real Spotify authorize URL as a link ->
 * the user consents, lands on the redirect_uri that deliberately fails to
 * load, copies it from the address bar, and pastes it back into this page.
 *
 * Lives in main/ rather than a component because it is the one place both
 * edges it needs are already legal: it calls svc_spotify (app -> svc) AND
 * esp_http_server (app -> idf). A svc_* component could do neither without
 * either a forbidden svc->svc edge or a callback-injection dance for one
 * caller. Precedent: main/debug_console_spotify.c, which bridges the same
 * service to the UART console the same way.
 *
 * The listener's lifetime is EXACTLY the pair overlay's -- started and
 * stopped by INTENT_DOMAIN_PAIRING, which that overlay's mount()/unmount()
 * submit. It does not run at boot, does not run while the panel is idle, and
 * does not outlive the screen. That bound is what makes reversing adr/0009's
 * "no on-device listener" position defensible, and it is the reason none of
 * this is behind a Kconfig "enable" flag: there is no always-on state to opt
 * out of.
 */
#ifndef PAIR_HTTP_H
#define PAIR_HTTP_H

#include <stdbool.h>

#include "app_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Idempotent -- a second call while already running is a no-op and, in
 * particular, does NOT regenerate the authorize URL.
 *
 * `force` true means the user explicitly asked to reconnect a different
 * account: unpair first, because spotify_session_fsm accepts PAIR_REQUESTED
 * only from UNPAIRED/REAUTH_REQUIRED and would otherwise silently ignore both
 * the pair request and the code paste that follows it.
 *
 * Returns false if the HTTP server could not be started. The pairing exchange
 * is deliberately begun only AFTER the server is up, so a false return also
 * guarantees no verifier/nonce was burned. */
bool pair_http_start(app_ctx_t *ctx, bool force);

void pair_http_stop(app_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PAIR_HTTP_H */
