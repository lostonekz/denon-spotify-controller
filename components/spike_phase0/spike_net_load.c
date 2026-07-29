/*
 * Phase 0: network + TLS load. THROWAWAY. See spike_phase0.h.
 *
 * The point is not to test HTTP. It is to put WiFi DMA and mbedTLS working
 * memory in contention with the LCD scanout, and to measure what a TLS session
 * actually costs in internal SRAM -- the number docs/MEMORY.md §4 currently
 * only estimates at ~40 KB (or ~25 KB with reduced fragment lengths).
 */

#include "spike_net_load.h"

#include <inttypes.h>   /* PRIu32 -- see spike_report.c for why this matters */
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "spike0.net";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_events;
static int s_retries;

/* Sampled from the HTTP event handler so we catch the peak DURING the
 * session, not after it has been torn down. */
static volatile uint32_t s_internal_min_during;

static void wifi_handler(void *arg, esp_event_base_t base,
                         int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < 10) {
            esp_wifi_connect();
            s_retries++;
            ESP_LOGW(TAG, "retry %d", s_retries);
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retries = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t spike_net_connect(const char *ssid, const char *password)
{
    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, NULL, NULL));

    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to %s", ssid);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "failed to connect to %s", ssid);
    return ESP_FAIL;
}

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        uint32_t f = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (f < s_internal_min_during) {
            s_internal_min_during = f;
        }
    }
    return ESP_OK;
}

void spike_net_download(const char *url, spike_net_result_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "download URL is empty");
        return;
    }

    uint32_t before = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_internal_min_during = before;

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = http_event,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size = 4096,
    };

    int64_t t0 = esp_timer_get_time();
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "client init failed");
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    int64_t t_handshake = esp_timer_get_time();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    esp_http_client_fetch_headers(client);

    char buf[1024];
    int total = 0;
    int n;
    while ((n = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        total += n;
        uint32_t f = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (f < s_internal_min_during) {
            s_internal_min_during = f;
        }
    }

    int64_t t1 = esp_timer_get_time();

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    out->bytes = (uint32_t)total;
    out->handshake_ms = (uint32_t)((t_handshake - t0) / 1000);
    out->tls_heap_peak = (before > s_internal_min_during)
                       ? (before - s_internal_min_during) : 0;
    double secs = (double)(t1 - t_handshake) / 1e6;
    out->kbps = (secs > 0.0) ? (double)total / 1024.0 / secs : 0.0;
    out->ok = total > 0;

    ESP_LOGI(TAG, "downloaded %d B, handshake %" PRIu32 " ms, "
                  "TLS internal peak %" PRIu32 " B",
             total, out->handshake_ms, out->tls_heap_peak);
}
