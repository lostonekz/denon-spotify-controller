#include "view_model_system_apply.h"

#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_wifi.h"

#include "view_model_system.h"

static void read_panel_id(char *buf, size_t buflen)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    /* No real serial number exists anywhere on this board -- the last 3
     * bytes of the station MAC are a real, stable, unique-per-device
     * identifier, not a fabricated string. */
    snprintf(buf, buflen, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

/* Long enough that this stops being a per-frame cost, short enough that a
 * user watching the System screen sees a signal-strength reading that
 * tracks reality. 1s matches the cadence the rest of this project already
 * treats as "live enough" for a status display (ui_app.c's own DOMAIN_UI
 * publish period, svc_diagnostics's own report tick is 5s). */
#define SYSTEM_RSSI_SAMPLE_PERIOD_US ((int64_t)1000 * 1000)

bool view_model_system_apply_init(view_model_system_apply_t *a, core_state_t *state,
                                      core_eventbus_t *bus, ui_router_t *router, time_source_t clock)
{
    a->state  = state;
    a->bus    = bus;
    a->router = router;
    a->clock  = clock;

    /* Both fixed for the life of the boot -- read once here instead of on
     * every tick. esp_app_get_description() returns a pointer into the
     * running image's own descriptor, so this stores a pointer, not a copy,
     * and it stays valid as long as the app is running.
     *
     * esp_app_get_description(), not esp_ota_get_app_description(): the
     * latter is the same function under a deprecated name (it warns
     * -Wdeprecated-declarations, which this build surfaces) and lives in
     * esp_ota_ops.h purely for backward compatibility. Same value, no
     * warning, and it drops this file's need for esp_ota_ops.h entirely. */
    a->app_desc = esp_app_get_description();
    read_panel_id(a->panel_id, sizeof(a->panel_id));

    a->rssi_dbm            = 0;
    a->rssi_sampled_us     = 0;
    a->rssi_ever_sampled = false;

    if (!core_eventbus_subscribe(bus, TOPIC_NET, a->net_sub_storage, 4, &a->net_sub)) {
        return false;
    }
    if (!core_eventbus_subscribe(bus, TOPIC_DENON, a->denon_sub_storage, 8, &a->denon_sub)) {
        return false;
    }
    if (!core_eventbus_subscribe(bus, TOPIC_SPOTIFY, a->spotify_sub_storage, 4, &a->spotify_sub)) {
        return false;
    }
    return true;
}

void view_model_system_apply_tick(view_model_system_apply_t *a)
{
    event_t evt;
    while (core_eventbus_poll(a->bus, a->net_sub, &evt))     { }
    while (core_eventbus_poll(a->bus, a->denon_sub, &evt))    { }
    while (core_eventbus_poll(a->bus, a->spotify_sub, &evt)) { }

    net_state_t     net     = { 0 };
    denon_state_t    denon   = { 0 };
    spotify_state_t spotify = { 0 };
    core_state_read(a->state, DOMAIN_NET, &net, sizeof(net));
    core_state_read(a->state, DOMAIN_DENON, &denon, sizeof(denon));
    core_state_read(a->state, DOMAIN_SPOTIFY, &spotify, sizeof(spotify));

    /* esp_wifi_sta_get_ap_info() is a real blocking call into the WiFi
     * driver, not a cheap field read -- at the ~60Hz this function is
     * driven at, it was the single most expensive thing on the UI task,
     * for a number the screen shows to the nearest dBm once a second.
     * Sampled on its own interval instead, reusing the last value in
     * between. The `!rssi_ever_sampled` term makes the first tick after
     * the link comes up sample immediately rather than showing 0 dBm for
     * up to a second. */
    if (net.link_up) {
        int64_t now_us = a->clock.now_us(a->clock.ctx);
        if (!a->rssi_ever_sampled ||
            (now_us - a->rssi_sampled_us) >= SYSTEM_RSSI_SAMPLE_PERIOD_US) {
            a->rssi_sampled_us     = now_us;
            a->rssi_ever_sampled = true;
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                a->rssi_dbm = ap_info.rssi;
            } else {
                a->rssi_dbm = 0;   /* associated a moment ago but no record yet --
                                        same "no reading" value as link-down below */
            }
        }
    } else {
        /* Link down: report no signal, and force a fresh sample the moment
         * it comes back rather than briefly showing a stale pre-drop value. */
        a->rssi_dbm            = 0;
        a->rssi_ever_sampled = false;
    }

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, a->rssi_dbm,
                                                 a->app_desc->version, a->app_desc->date,
                                                 a->panel_id, a->clock);
    ui_router_update(a->router, UI_SCREEN_SYSTEM, &vm);
}
