#include "net_time.h"

#include <string.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

static const char *TAG = "net_time";

bool net_time_create(net_time_t *t, const net_time_cfg_t *cfg)
{
    memset(t, 0, sizeof(*t));
    t->bus          = cfg->bus;
    t->sntp_server  = cfg->sntp_server;

    if (!core_eventbus_subscribe(t->bus, TOPIC_NET, t->net_sub_storage,
                                   sizeof(t->net_sub_storage) / sizeof(t->net_sub_storage[0]),
                                   &t->net_sub)) {
        ESP_LOGE(TAG, "failed to subscribe to TOPIC_NET (no policy registered?)");
        return false;
    }

    return true;
}

static void start_sntp(net_time_t *t)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(t->sntp_server);
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init failed");
        return;
    }
    t->sntp_started = true;
    ESP_LOGI(TAG, "SNTP started against %s", t->sntp_server);
}

void net_time_tick(net_time_t *t)
{
    if (t->synced) {
        return;
    }

    if (!t->sntp_started) {
        event_t evt;
        while (core_eventbus_poll(t->bus, t->net_sub, &evt)) {
            if (evt.topic == TOPIC_NET && evt.u.net.kind == NET_EVT_IP_ACQUIRED) {
                t->ip_seen = true;
            }
        }
        if (t->ip_seen) {
            start_sntp(t);
        }
        return;
    }

    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        t->synced = true;

        event_t out = { .topic = TOPIC_TIME };
        out.u.time.synced = true;
        core_eventbus_publish(t->bus, &out);

        ESP_LOGI(TAG, "time synced");
    }
}
