#include "net_wifi_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "net_wifi_softap";

void net_wifi_softap_start(net_wifi_t *w)
{
    net_wifi_ap_password_from_mac(w->mac, w->ap_password);

    snprintf(w->ap_ssid, sizeof(w->ap_ssid), "%s-%02x%02x%02x",
             (w->ap_ssid_prefix != NULL) ? w->ap_ssid_prefix : "DSC-Setup",
             w->mac[3], w->mac[4], w->mac[5]);

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy((char *)cfg.ap.ssid, w->ap_ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = (uint8_t)strlen(w->ap_ssid);
    strncpy((char *)cfg.ap.password, w->ap_password, sizeof(cfg.ap.password));
    cfg.ap.max_connection = 2;
    cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    cfg.ap.channel        = 1;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &cfg);

    if (!w->wifi_started) {
        if (esp_wifi_start() != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start (AP) failed");
            w->last_action_ok = false;
            return;
        }
        w->wifi_started = true;
    }

    /* Stand-in for the on-screen QR + pairing PIN (Phase 5, no hal_display/
     * ui yet): logged over UART instead, per the user's Phase 2 scoping
     * decision -- see net_wifi.h's top comment. */
    ESP_LOGW(TAG, "provisioning: join WiFi network '%s' with password '%s'",
             w->ap_ssid, w->ap_password);

    w->last_action_ok = true;
}

void net_wifi_softap_stop(net_wifi_t *w)
{
    (void)w;
    /* act_start_connect() switches the mode back to STA right after this
     * (via the CREDENTIALS_SAVED -> start_connect chain); nothing else to
     * tear down at the esp_wifi level itself. */
}
