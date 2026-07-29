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
#include "freertos/semphr.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

/* Small, different host than the main spike target -- see spike_net_diag(). */
#define SPIKE_NET_DIAG_SMALL_URL "https://www.google.com/robots.txt"

static const char *TAG = "spike0.net";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* No Range header: the configured URL (Cloudflare's speed-test endpoint,
 * see main/Kconfig.projbuild) takes its size via a query parameter and
 * ignores Range entirely. A prior version used a 20 MiB Range request
 * against a different host (Hetzner) that consistently failed every
 * attempt with a TLS read error while parsing response headers -- a small
 * download from a THIRD host (google.com) using this exact same code path
 * succeeded immediately, which ruled out this board's WiFi/TLS/HTTP stack
 * and the Range mechanism itself as the cause. Root cause on the Hetzner
 * side was never identified; switched hosts instead of continuing to
 * debug a specific third-party server's behavior. */

static EventGroupHandle_t s_wifi_events;
static int s_retries;

/* Sampled from the HTTP event handler so we catch the peak DURING the
 * session, not after it has been torn down. */
static volatile uint32_t s_internal_min_during;

typedef struct {
    uint32_t bytes;
    int64_t  t_connected_us;
} net_dl_ctx_t;

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
    net_dl_ctx_t *ctx = (net_dl_ctx_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_CONNECTED) {
        /* TCP + TLS handshake done, no request/response exchanged yet -- the
         * same boundary the old open()/fetch_headers() split measured. */
        if (ctx != NULL) {
            ctx->t_connected_us = esp_timer_get_time();
        }
    } else if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (ctx != NULL) {
            ctx->bytes += (uint32_t)evt->data_len;
        }
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

    net_dl_ctx_t ctx = { .bytes = 0, .t_connected_us = 0 };

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = http_event,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .buffer_size = 4096,
        .user_data = &ctx,
        /* esp_http_client_perform() below follows redirects itself (up to
         * the default of 10). The manual open()/fetch_headers()/read()
         * sequence this replaced does NOT -- it silently returned whatever
         * short redirect body the server sent (~146 B for this URL) instead
         * of the 10 MB file, so every "download" measurement was fictional:
         * no real sustained WiFi/PSRAM bus traffic, no real transfer rate. */
    };

    int64_t t0 = esp_timer_get_time();
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "client init failed");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    int64_t t1 = esp_timer_get_time();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "perform: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    int status = esp_http_client_get_status_code(client);
    int64_t t_handshake = (ctx.t_connected_us != 0) ? ctx.t_connected_us : t0;

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    out->bytes = ctx.bytes;
    out->handshake_ms = (uint32_t)((t_handshake - t0) / 1000);
    out->tls_heap_peak = (before > s_internal_min_during)
                       ? (before - s_internal_min_during) : 0;
    double secs = (double)(t1 - t_handshake) / 1e6;
    out->kbps = (secs > 0.0) ? (double)ctx.bytes / 1024.0 / secs : 0.0;
    /* 206 Partial Content is the expected success status for a Range
     * request; a plain 200 means the server ignored the Range header and
     * sent the whole file, which is still a real, valid transfer. */
    out->ok = (status == 200 || status == 206) && (ctx.bytes > 0);

    ESP_LOGI(TAG, "downloaded %" PRIu32 " B, status %d, handshake %" PRIu32 " ms, "
                  "TLS internal peak %" PRIu32 " B",
             out->bytes, status, out->handshake_ms, out->tls_heap_peak);
}

/* ---- diagnostic: ping + small download from a different host ---- */

static volatile bool s_ping_got_reply;

static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint32_t elapsed_ms = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_ms, sizeof(elapsed_ms));
    ESP_LOGI(TAG, "  reply in %" PRIu32 " ms", elapsed_ms);
    s_ping_got_reply = true;
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    (void)hdl;
    (void)args;
    ESP_LOGW(TAG, "  timeout");
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    (void)hdl;
    xSemaphoreGive((SemaphoreHandle_t)args);
}

/* Sequential, not concurrent -- s_ping_got_reply and the single ping task
 * this creates are fine to reuse across calls. Blocks for up to ~count *
 * timeout_ms, plus a little, per call. */
static bool ping_host(const char *label, const ip_addr_t *target)
{
    ESP_LOGI(TAG, "ping %s ...", label);
    s_ping_got_reply = false;

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (done == NULL) {
        return false;
    }

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = *target;
    cfg.count = 3;

    esp_ping_callbacks_t cbs = {
        .cb_args         = done,
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end     = on_ping_end,
    };

    esp_ping_handle_t hdl;
    if (esp_ping_new_session(&cfg, &cbs, &hdl) != ESP_OK) {
        ESP_LOGE(TAG, "ping %s: session create failed", label);
        vSemaphoreDelete(done);
        return false;
    }

    esp_ping_start(hdl);
    xSemaphoreTake(done, pdMS_TO_TICKS(10000));
    esp_ping_delete_session(hdl);
    vSemaphoreDelete(done);

    return s_ping_got_reply;
}

void spike_net_diag(void)
{
    ESP_LOGI(TAG, "=== network diagnostic: ping + small download from a "
                  "different host, before the real spike load ===");

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        ip_addr_t gw = IPADDR4_INIT(ip_info.gw.addr);
        bool gw_ok = ping_host("gateway", &gw);
        ESP_LOGI(TAG, "ping gateway: %s", gw_ok ? "OK" : "FAILED / no reply");
    } else {
        ESP_LOGW(TAG, "could not read gateway IP -- skipping gateway ping");
    }

    ip_addr_t public_ip;
    ipaddr_aton("8.8.8.8", &public_ip);
    bool public_ok = ping_host("8.8.8.8", &public_ip);
    ESP_LOGI(TAG, "ping 8.8.8.8: %s", public_ok ? "OK" : "FAILED / no reply");

    spike_net_result_t r;
    spike_net_download(SPIKE_NET_DIAG_SMALL_URL, &r);
    ESP_LOGI(TAG, "small download (%s): %s, %" PRIu32 " bytes",
             SPIKE_NET_DIAG_SMALL_URL, r.ok ? "OK" : "FAILED", r.bytes);

    ESP_LOGI(TAG, "=== end network diagnostic ===");
}
