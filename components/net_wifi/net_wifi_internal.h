/*
 * Private declarations shared across net_wifi.c/net_wifi_softap.c/
 * net_wifi_portal_http.c/net_wifi_portal_dns.c. Not installed publicly --
 * only these four source files in this directory include it.
 */
#ifndef NET_WIFI_INTERNAL_H
#define NET_WIFI_INTERNAL_H

#include "net_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Safe from any task context: a full queue drops the message (logged) with
 * a short wait, rather than ever blocking an ESP-IDF event/HTTP-server
 * task indefinitely. */
void net_wifi_post_msg(net_wifi_t *w, net_wifi_msg_type_t type, bool ok);

/* Brings up esp_wifi in AP mode with an SSID/password derived from the
 * station MAC, and logs both over UART (this phase's stand-in for the
 * on-screen QR/PIN -- see net_wifi.h). Synchronous; sets w->last_action_ok. */
void net_wifi_softap_start(net_wifi_t *w);
void net_wifi_softap_stop(net_wifi_t *w);

/* esp_http_server: GET / (scan list + form), POST /save, catch-all
 * redirect (captive-portal trigger). Synchronous; sets w->last_action_ok. */
void net_wifi_portal_http_start(net_wifi_t *w);
void net_wifi_portal_http_stop(net_wifi_t *w);

/* Minimal UDP responder on port 53: replies the AP's own IP to every
 * query while the portal is active, which is what makes a phone's OS
 * offer to open the captive portal automatically. Synchronous. */
void net_wifi_portal_dns_start(net_wifi_t *w);
void net_wifi_portal_dns_stop(net_wifi_t *w);

/* net_wifi_fsm / net_wifi_prov_fsm action implementations (net_wifi_actions.c),
 * wired into the vtables net_wifi_create() builds. Split into their own file
 * to keep net_wifi.c under the project's 400-line-per-file limit. */
void net_wifi_act_start_provisioning(void *ctx);
void net_wifi_act_start_connect(void *ctx);
void net_wifi_act_enter_syncing(void *ctx);
void net_wifi_act_enter_online(void *ctx);
void net_wifi_act_enter_degraded(void *ctx);
void net_wifi_act_start_backoff(void *ctx);

void net_wifi_act_prov_start_ap(void *ctx);
void net_wifi_act_prov_ap_active(void *ctx);
void net_wifi_act_prov_begin_save(void *ctx, const void *credentials);
void net_wifi_act_prov_save_failed(void *ctx);
void net_wifi_act_prov_stop_ap(void *ctx);
void net_wifi_act_prov_done(void *ctx);
void net_wifi_act_prov_failed(void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* NET_WIFI_INTERNAL_H */
