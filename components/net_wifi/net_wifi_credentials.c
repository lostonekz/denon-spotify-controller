#include "net_wifi_credentials.h"

#include <string.h>

#define NET_WIFI_KEY_SSID "wifi.ssid"
#define NET_WIFI_KEY_PSK  "wifi.psk"

bool net_wifi_credentials_load(kv_store_if_t *kv, char ssid_out[NET_WIFI_SSID_MAX],
                                  char psk_out[NET_WIFI_PSK_MAX])
{
    size_t ssid_len = NET_WIFI_SSID_MAX;
    if (!kv->get_blob(kv->ctx, NET_WIFI_KEY_SSID, ssid_out, &ssid_len) ||
        ssid_len != NET_WIFI_SSID_MAX) {
        return false;
    }

    size_t psk_len = NET_WIFI_PSK_MAX;
    if (!kv->get_blob(kv->ctx, NET_WIFI_KEY_PSK, psk_out, &psk_len) ||
        psk_len != NET_WIFI_PSK_MAX) {
        return false;
    }

    return true;
}

bool net_wifi_credentials_save(kv_store_if_t *kv, const char *ssid, const char *psk)
{
    char ssid_buf[NET_WIFI_SSID_MAX] = { 0 };
    char psk_buf[NET_WIFI_PSK_MAX]   = { 0 };

    strncpy(ssid_buf, ssid, NET_WIFI_SSID_MAX - 1);
    strncpy(psk_buf, psk, NET_WIFI_PSK_MAX - 1);

    if (!kv->set_blob(kv->ctx, NET_WIFI_KEY_SSID, ssid_buf, sizeof(ssid_buf))) {
        return false;
    }
    if (!kv->set_blob(kv->ctx, NET_WIFI_KEY_PSK, psk_buf, sizeof(psk_buf))) {
        return false;
    }
    return kv->commit(kv->ctx);
}

bool net_wifi_credentials_clear(kv_store_if_t *kv)
{
    kv->erase_key(kv->ctx, NET_WIFI_KEY_SSID);
    kv->erase_key(kv->ctx, NET_WIFI_KEY_PSK);
    return kv->commit(kv->ctx);
}
