#include "view_model_system_apply.h"

#include <stdio.h>

#include "esp_mac.h"
#include "esp_ota_ops.h"
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

bool view_model_system_apply_init(view_model_system_apply_t *a, core_state_t *state,
                                      core_eventbus_t *bus, ui_router_t *router, time_source_t clock)
{
    a->state  = state;
    a->bus    = bus;
    a->router = router;
    a->clock  = clock;

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

    int8_t rssi_dbm = 0;
    if (net.link_up) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi_dbm = ap_info.rssi;
        }
    }

    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    char                     panel_id[32];
    read_panel_id(panel_id, sizeof(panel_id));

    system_vm_t vm = view_model_system_build(&net, &denon, &spotify, rssi_dbm,
                                                 app_desc->version, app_desc->date, panel_id, a->clock);
    ui_router_update(a->router, UI_SCREEN_SYSTEM, &vm);
}
