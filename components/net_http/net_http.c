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

/* Generalized transport open: method + optional bearer token + optional
 * body. net_http_stream_open() (below) is the GET-only, no-auth, no-body
 * public entry point OTA's streaming download already uses, now a thin
 * wrapper over this. *out_reached_server is set true as soon as
 * esp_http_client_open() succeeds -- i.e. the request may have reached the
 * server -- regardless of what happens afterward; net_http_request() uses
 * it to distinguish a pre-send failure (always safely retryable) from a
 * post-send ambiguous one (respects opts->retryable_after_send). */
static bool stream_open_ex(const char *url, const net_http_request_opts_t *opts,
                             const http_policy_t *policy, net_http_stream_t *stream,
                             int *out_status_code, uint32_t *out_retry_after_s,
                             bool *out_reached_server)
{
    *out_reached_server = false;

    esp_http_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.url               = url;
    config.timeout_ms        = policy->request_timeout_ms;
    config.crt_bundle_attach  = esp_crt_bundle_attach;
    switch (opts->method) {
    case NET_HTTP_METHOD_POST: config.method = HTTP_METHOD_POST; break;
    case NET_HTTP_METHOD_PUT:  config.method = HTTP_METHOD_PUT;  break;
    default:                    config.method = HTTP_METHOD_GET;  break;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    if (opts->bearer_token != NULL) {
        char header[512];
        snprintf(header, sizeof(header), "Bearer %s", opts->bearer_token);
        esp_http_client_set_header(client, "Authorization", header);
    }
    size_t body_len = (opts->body.body != NULL) ? opts->body.body_len : 0;
    if (body_len > 0 && opts->body.content_type != NULL) {
        esp_http_client_set_header(client, "Content-Type", opts->body.content_type);
    }

    esp_err_t err = esp_http_client_open(client, (int)body_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_http_client_open(%s) failed: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    *out_reached_server = true;

    if (body_len > 0) {
        int written = esp_http_client_write(client, (const char *)opts->body.body, (int)body_len);
        if (written < 0 || (size_t)written != body_len) {
            ESP_LOGW(TAG, "esp_http_client_write(%s) failed", url);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
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

bool net_http_stream_open(const char *url, const http_policy_t *policy,
                             net_http_stream_t *stream, int *out_status_code,
                             uint32_t *out_retry_after_s)
{
    net_http_request_opts_t opts = { .method = NET_HTTP_METHOD_GET };
    bool reached_server;
    return stream_open_ex(url, &opts, policy, stream, out_status_code, out_retry_after_s,
                            &reached_server);
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

bool net_http_request(net_http_t *h, const char *url, const net_http_request_opts_t *opts,
                        const http_policy_t *policy, char *out_buf, size_t out_cap,
                        net_http_response_t *resp)
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
        bool reached_server = false;
        bool opened = stream_open_ex(url, opts, policy, &stream, &status_code, &retry_after_s,
                                        &reached_server);

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

        if (reached_server && failed && !opts->retryable_after_send) {
            /* The request may have reached the server with no definite
             * response back -- for a side-effecting call, one attempt is
             * final; see net_http_request_opts_t.retryable_after_send. */
            resp->status_code = status_code;
            resp->len          = total;
            return opened;
        }

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

bool net_http_get(net_http_t *h, const char *url, const http_policy_t *policy,
                    char *out_buf, size_t out_cap, net_http_response_t *resp)
{
    net_http_request_opts_t opts = { .method = NET_HTTP_METHOD_GET, .retryable_after_send = true };
    return net_http_request(h, url, &opts, policy, out_buf, out_cap, resp);
}
