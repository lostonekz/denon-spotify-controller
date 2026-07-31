/*
 * net_wifi_fsm / net_wifi_prov_fsm real action implementations. Split out
 * of net_wifi.c to stay under this project's 400-line-per-file limit
 * (tools/check_limits.py) -- the two files together are one logical unit,
 * sharing net_wifi_internal.h.
 */
#include "net_wifi_internal.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define NET_WIFI_SNTP_TIMEOUT_MS (30 * 1000)

static const char *TAG = "net_wifi";

/* --- reporting into core_state, preserving whatever the domain payload
 * already holds except the fields this glue is actually changing --- */

static void report_net_status(net_wifi_t *w, link_status_t status)
{
    net_state_t s;
    if (!core_state_read(w->state, DOMAIN_NET, &s, sizeof(s))) {
        memset(&s, 0, sizeof(s));
    }
    core_state_write(w->state, DOMAIN_NET, status, &s, sizeof(s));
}

static void report_net_ip_acquired(net_wifi_t *w)
{
    esp_netif_ip_info_t ip_info;
    net_state_t s;
    if (!core_state_read(w->state, DOMAIN_NET, &s, sizeof(s))) {
        memset(&s, 0, sizeof(s));
    }
    if (esp_netif_get_ip_info(w->sta_netif, &ip_info) == ESP_OK) {
        s.link_up = true;
        s.ipv4    = ip_info.ip.addr;
    }
    core_state_write(w->state, DOMAIN_NET, LINK_STATUS_OK, &s, sizeof(s));

    event_t evt = { .topic = TOPIC_NET };
    evt.u.net.kind = NET_EVT_IP_ACQUIRED;
    evt.u.net.ipv4 = s.ipv4;
    core_eventbus_publish(w->bus, &evt);
}

/* Reused for both the reconnect backoff and the SNTP-sync timeout, since a
 * device is never in RECONNECTING and SYNCING at once (net_wifi.c's
 * handle_retry_timer() picks the right meaning from the current state). */
static void arm_timer(net_wifi_t *w, uint32_t delay_ms)
{
    xTimerChangePeriod(w->retry_timer, pdMS_TO_TICKS(delay_ms > 0 ? delay_ms : 1), 0);
}

/* --- net_wifi_fsm action implementations --- */

void net_wifi_act_start_provisioning(void *ctx)
{
    net_wifi_t *w = (net_wifi_t *)ctx;
    net_wifi_prov_fsm_dispatch(&w->prov_fsm, PROV_EVT_START, NULL);
    /* start_ap (this dispatch's own action) already ran synchronously and
     * set w->last_action_ok -- prov_fsm's state is now committed to
     * AP_STARTING, so this second, separate dispatch is safe. */
    net_wifi_prov_fsm_dispatch(&w->prov_fsm,
                                 w->last_action_ok ? PROV_EVT_AP_UP : PROV_EVT_AP_START_FAILED,
                                 NULL);
}

void net_wifi_act_start_connect(void *ctx)
{
    net_wifi_t *w = (net_wifi_t *)ctx;

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy((char *)cfg.sta.ssid, w->sta_ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, w->sta_psk, sizeof(cfg.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);

    if (!w->wifi_started) {
        esp_wifi_start();
        w->wifi_started = true;
        /* esp_wifi_start() itself triggers WIFI_EVENT_STA_START, which
         * this glue doesn't act on; the connect attempt below is what
         * actually starts association. */
    }
    esp_wifi_connect();
}

void net_wifi_act_enter_syncing(void *ctx)
{
    net_wifi_t *w = (net_wifi_t *)ctx;
    report_net_ip_acquired(w);
    arm_timer(w, NET_WIFI_SNTP_TIMEOUT_MS);
}

void net_wifi_act_enter_online(void *ctx)
{
    net_wifi_t *w = (net_wifi_t *)ctx;
    net_wifi_backoff_reset(&w->backoff);
    report_net_status(w, LINK_STATUS_OK);
}

void net_wifi_act_enter_degraded(void *ctx)
{
    net_wifi_t *w = (net_wifi_t *)ctx;
    report_net_status(w, LINK_STATUS_STALE);
}

void net_wifi_act_start_backoff(void *ctx)
{
    net_wifi_t *w = (net_wifi_t *)ctx;
    report_net_status(w, LINK_STATUS_RECONNECTING);
    uint32_t delay_ms = net_wifi_backoff_next_delay_ms(&w->backoff);
    ESP_LOGI(TAG, "reconnecting in %" PRIu32 " ms", delay_ms);
    arm_timer(w, delay_ms);
}

/* --- net_wifi_prov_fsm action implementations --- */

void net_wifi_act_prov_start_ap(void *ctx)
{
    net_wifi_t *w = (net_wifi_t *)ctx;
    net_wifi_softap_start(w);          /* sets w->last_action_ok */
    if (w->last_action_ok) {
        net_wifi_portal_dns_start(w);
        net_wifi_portal_http_start(w); /* may override last_action_ok on failure */
    }
}

void net_wifi_act_prov_ap_active(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "provisioning portal ready");
}

void net_wifi_act_prov_begin_save(void *ctx, const void *credentials)
{
    (void)ctx; (void)credentials;
    ESP_LOGI(TAG, "credentials submitted, saving");
}

void net_wifi_act_prov_save_failed(void *ctx)
{
    (void)ctx;
    ESP_LOGW(TAG, "failed to save credentials -- portal stays up for retry");
}

void net_wifi_act_prov_stop_ap(void *ctx)
{
    net_wifi_t *w = (net_wifi_t *)ctx;
    net_wifi_portal_http_stop(w);
    net_wifi_portal_dns_stop(w);
    net_wifi_softap_stop(w);
}

void net_wifi_act_prov_done(void *ctx)
{
    net_wifi_t *w = (net_wifi_t *)ctx;
    /* Dispatching on w->fsm (a different core_fsm_t instance than
     * prov_fsm, whose action is currently running) is safe -- only
     * recursing into the SAME instance mid-action is not. */
    net_wifi_fsm_dispatch(&w->fsm, NET_WIFI_EVT_CREDENTIALS_SAVED, NULL);
}

void net_wifi_act_prov_failed(void *ctx)
{
    (void)ctx;
    ESP_LOGE(TAG, "provisioning AP failed to start");
}
