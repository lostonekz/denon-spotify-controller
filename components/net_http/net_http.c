#include "net_http.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#define NET_HTTP_TLS_MAX_SLOTS 2

static const char *TAG = "net_http";

bool net_http_create(net_http_t *h)
{
    memset(h, 0, sizeof(*h));
    h->tls_slots = xSemaphoreCreateCounting(NET_HTTP_TLS_MAX_SLOTS, NET_HTTP_TLS_MAX_SLOTS);
    return h->tls_slots != NULL;
}

bool net_http_tls_acquire(net_http_t *h, uint8_t n_slots, uint32_t timeout_ms)
{
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    for (uint8_t i = 0; i < n_slots; i++) {
        if (xSemaphoreTake(h->tls_slots, timeout_ticks) != pdTRUE) {
            for (uint8_t j = 0; j < i; j++) {
                xSemaphoreGive(h->tls_slots);
            }
            return false;
        }
    }
    return true;
}

void net_http_tls_release(net_http_t *h, uint8_t n_slots)
{
    for (uint8_t i = 0; i < n_slots; i++) {
        xSemaphoreGive(h->tls_slots);
    }
}

bool net_http_stream_open(const char *url, const http_policy_t *policy,
                             net_http_stream_t *stream, int *out_status_code,
                             uint32_t *out_retry_after_s)
{
    esp_http_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.url               = url;
    config.timeout_ms        = policy->request_timeout_ms;
    config.crt_bundle_attach  = esp_crt_bundle_attach;
    config.method             = HTTP_METHOD_GET;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_http_client_open(%s) failed: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    if (esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGW(TAG, "esp_http_client_fetch_headers(%s) failed", url);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    *out_status_code = esp_http_client_get_status_code(client);

    *out_retry_after_s = 0;
    char *retry_after_val = NULL;
    if (esp_http_client_get_header(client, "Retry-After", &retry_after_val) == ESP_OK &&
        retry_after_val != NULL) {
        int parsed = atoi(retry_after_val);
        if (parsed > 0) {
            *out_retry_after_s = (uint32_t)parsed;
        }
    }

    stream->client = client;
    return true;
}

int net_http_stream_read(net_http_stream_t *stream, void *buf, size_t buf_len)
{
    return esp_http_client_read((esp_http_client_handle_t)stream->client, (char *)buf, (int)buf_len);
}

void net_http_stream_close(net_http_stream_t *stream)
{
    if (stream->client != NULL) {
        esp_http_client_handle_t client = (esp_http_client_handle_t)stream->client;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        stream->client = NULL;
    }
}

bool net_http_get(net_http_t *h, const char *url, const http_policy_t *policy,
                    char *out_buf, size_t out_cap, net_http_response_t *resp)
{
    for (uint8_t attempt = 0; ; attempt++) {
        if (!net_http_tls_acquire(h, 1, policy->connect_timeout_ms)) {
            ESP_LOGE(TAG, "no TLS slot available for %s", url);
            resp->status_code = 0;
            resp->len          = 0;
            return false;
        }

        net_http_stream_t stream = { 0 };
        int status_code = 0;
        uint32_t retry_after_s = 0;
        bool opened = net_http_stream_open(url, policy, &stream, &status_code, &retry_after_s);

        bool   failed = !opened;
        size_t total  = 0;

        if (opened) {
            for (;;) {
                if (total >= out_cap) {
                    break;   /* buffer full -- truncate, not an error */
                }
                int n = net_http_stream_read(&stream, out_buf + total, out_cap - total);
                if (n < 0) {
                    failed = true;
                    break;
                }
                if (n == 0) {
                    break;   /* EOF */
                }
                total += (size_t)n;
            }
            net_http_stream_close(&stream);
        }

        net_http_tls_release(h, 1);

        uint32_t delay_ms = 0;
        http_retry_decision_t decision = net_http_retry_decide(policy, attempt, status_code,
                                                                   failed, retry_after_s, &delay_ms);
        if (decision == HTTP_RETRY_NO) {
            resp->status_code = status_code;
            resp->len          = total;
            return opened;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
