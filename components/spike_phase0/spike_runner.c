/*
 * Phase 0 orchestration. THROWAWAY. See spike_phase0.h.
 *
 * Runs every load source SIMULTANEOUSLY, because that is the only
 * configuration in which R1 manifests. Measuring them one at a time produces
 * four reassuring numbers and zero information.
 */

#include "spike_phase0.h"

#include <inttypes.h>   /* PRIu32 -- see spike_report.c for why this matters */
#include <string.h>

#include "spike_display_load.h"
#include "spike_net_load.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "spike0.run";

typedef struct {
    volatile bool     running;
    volatile uint32_t jpeg_decodes;
    volatile uint32_t jpeg_ms_total;
    volatile uint32_t jpeg_ms_max;
    volatile uint32_t nvs_writes;
    volatile uint32_t nvs_ms_total;
    volatile uint32_t nvs_ms_max;
    volatile uint32_t drops_at_nvs_start;
    volatile uint32_t drops_during_nvs;
    volatile uint32_t drops_during_jpeg;
    volatile uint32_t internal_min;
    volatile uint32_t psram_min;
    volatile uint32_t tls_heap_peak;
    volatile uint32_t tls_handshake_ms;
    volatile uint32_t bytes_downloaded;
    volatile float    download_kbps;
    spike_display_t  *disp;
    const spike_config_t *cfg;
} spike_ctx_t;

static spike_ctx_t g_ctx;

/* ---------------------------------------------------------------- */

static esp_err_t init_nvs_for_spike(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    }
    return err;
}

/*
 * JPEG load. A real 640x640 decode at 1/2 scale is what svc_artwork will do
 * on every track change (docs/MEMORY.md §1).
 *
 * PLACEHOLDER: this synthesises equivalent memory traffic rather than decoding
 * a real JPEG, because embedding a test image is a Phase 0 setup step the
 * operator does with their own artwork. Swap in esp_jpeg with a real 640x640
 * asset before trusting the timing number -- a synthetic pass here does NOT
 * validate decode timing, only bus contention.
 */
static void jpeg_load_task(void *arg)
{
    spike_ctx_t *c = (spike_ctx_t *)arg;

    const size_t out_px = 320 * 320;
    uint16_t *dst = heap_caps_malloc(out_px * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    uint8_t  *src = heap_caps_malloc(96 * 1024, MALLOC_CAP_SPIRAM);

    if (dst == NULL || src == NULL) {
        ESP_LOGE(TAG, "jpeg load: allocation failed (dst=%p src=%p)",
                 (void *)dst, (void *)src);
        free(dst);
        free(src);
        vTaskDelete(NULL);
        return;
    }

    memset(src, 0xA5, 96 * 1024);

    while (c->running) {
        /* Same before/after drop-attribution technique as nvs_load_task
         * below: sample the drop counter immediately before and after the
         * bus-heavy work, so any drop landing inside this window is
         * attributable to THIS task rather than lumped into the overall
         * rate. Two fallbacks (larger bounce buffers, 50 Hz refresh) left
         * the drop rate essentially unchanged at ~1.5% regardless of
         * refresh rate, which pointed at discrete contention events rather
         * than continuous bandwidth pressure -- this confirms or refutes
         * JPEG decode specifically as that event, the same way
         * frames_dropped_during_nvs already does for NVS writes. */
        uint32_t f0, v0, d0, l0;
        float m0;
        spike_display_stats(c->disp, &f0, &v0, &d0, &l0, &m0);

        int64_t t0 = esp_timer_get_time();

        /* Traffic shape stands in for a 1/2-scale decode: read compressed
         * source, write RGB565 output, PSRAM on both sides. */
        for (size_t i = 0; i < out_px; i++) {
            uint8_t a = src[(i * 7u) % (96u * 1024u)];
            uint8_t b = src[(i * 13u) % (96u * 1024u)];
            dst[i] = (uint16_t)((a << 8) | b);
        }

        uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        c->jpeg_decodes++;
        c->jpeg_ms_total += ms;
        if (ms > c->jpeg_ms_max) {
            c->jpeg_ms_max = ms;
        }

        uint32_t f1, v1, d1, l1;
        float m1;
        spike_display_stats(c->disp, &f1, &v1, &d1, &l1, &m1);
        (void)f0; (void)f1; (void)v0; (void)v1; (void)l0; (void)l1; (void)m0; (void)m1;
        c->drops_during_jpeg += (d1 - d0);

        vTaskDelay(pdMS_TO_TICKS(1500));   /* roughly a track change */
    }

    free(dst);
    free(src);
    vTaskDelete(NULL);
}

/*
 * NVS load. A flash write stalls the SPI bus, and on some parts stalls cache
 * fills too -- which is exactly the kind of thing that drops a frame. We count
 * frames dropped specifically during writes to attribute it.
 */
static void nvs_load_task(void *arg)
{
    spike_ctx_t *c = (spike_ctx_t *)arg;
    nvs_handle_t h;

    if (nvs_open("spike", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed");
        vTaskDelete(NULL);
        return;
    }

    uint32_t counter = 0;
    while (c->running) {
        uint32_t f0, v0, d0, l0;
        float m0;
        spike_display_stats(c->disp, &f0, &v0, &d0, &l0, &m0);

        int64_t t0 = esp_timer_get_time();
        nvs_set_u32(h, "counter", counter++);
        nvs_commit(h);
        uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);

        uint32_t f1, v1, d1, l1;
        float m1;
        spike_display_stats(c->disp, &f1, &v1, &d1, &l1, &m1);
        (void)v0; (void)v1;

        c->nvs_writes++;
        c->nvs_ms_total += ms;
        if (ms > c->nvs_ms_max) {
            c->nvs_ms_max = ms;
        }
        c->drops_during_nvs += (d1 - d0);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    nvs_close(h);
    vTaskDelete(NULL);
}

static void net_load_task(void *arg)
{
    spike_ctx_t *c = (spike_ctx_t *)arg;
    spike_net_result_t r;

    /* One bounded download for the run, not a reconnect loop: hitting the
     * same public host with a fresh TLS handshake every ~2 s for two
     * minutes reads as abuse to their infrastructure, not a real client --
     * every connection but the last got dropped mid-response or 429'd
     * during Phase 0 bring-up. One real connection streaming a bounded
     * range (see SPIKE_NET_RANGE_BYTES) is both what the product actually
     * does (one OTA/artwork fetch, not a poll loop) and keeps the WiFi/PSRAM
     * bus genuinely busy without tripping rate limiting. */
    spike_net_download(c->cfg->download_url, &r);
    if (r.ok) {
        c->tls_heap_peak    = r.tls_heap_peak;
        c->tls_handshake_ms = r.handshake_ms;
        c->bytes_downloaded = r.bytes;
        c->download_kbps    = r.kbps;
    }

    while (c->running) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    vTaskDelete(NULL);
}

/* Samples both heaps continuously; the minimum under combined load is the
 * number docs/MEMORY.md §1 needs. */
static void watermark_task(void *arg)
{
    spike_ctx_t *c = (spike_ctx_t *)arg;

    c->internal_min = 0xFFFFFFFFu;
    c->psram_min    = 0xFFFFFFFFu;

    while (c->running) {
        uint32_t i = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        uint32_t p = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (i < c->internal_min) {
            c->internal_min = i;
        }
        if (p < c->psram_min) {
            c->psram_min = p;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}

typedef struct {
    spike_display_t *disp;
    uint32_t          duration_s;
    volatile bool     done;
} render_ctx_t;

/*
 * Runs the render loop. Must be its own task pinned to core 1: app_main runs
 * on core 0 (CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y), the same core as every
 * load task above, so calling spike_display_render_frame() inline in
 * spike_phase0_run() put the render loop on core 0 too -- the single-core
 * contention the "core 0 vs core 1" split below is supposed to avoid. Fixing
 * that cut the drop rate from ~20% to ~1.5% on real hardware.
 *
 * spike_display_reset_timing() matters here: spike_display_init() runs
 * before the blocking spike_net_connect() call (WiFi association + DHCP),
 * so without resetting the timing baseline right before this loop starts,
 * frame #1's measured duration silently includes that wait and gets
 * misreported as a ~1.3 s render stall -- which is what a single
 * unexplained "longest frame" outlier turned out to be on every run before
 * this fix, confirmed by adding per-frame logging: exactly one slow frame,
 * always frame #1, never recurring.
 */
static void render_task(void *arg)
{
    render_ctx_t *r = (render_ctx_t *)arg;
    int64_t  end_us = esp_timer_get_time() + (int64_t)r->duration_s * 1000000;
    uint32_t phase = 0;

    spike_display_reset_timing(r->disp);

    while (esp_timer_get_time() < end_us) {
        spike_display_render_frame(r->disp, phase++);
        /* No delay: render as fast as the panel allows. Throttling here would
         * hide exactly the contention we are looking for. */
    }

    r->done = true;
    vTaskDelete(NULL);
}

/* ---------------------------------------------------------------- */

void spike_phase0_run(const spike_config_t *cfg, spike_report_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    if (cfg == NULL || cfg->duration_s == 0u) {
        ESP_LOGE(TAG, "invalid Phase 0 spike config");
        return;
    }

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.cfg = cfg;

    ESP_LOGI(TAG, "Phase 0 spike: %" PRIu32 " s of combined load", cfg->duration_s);

    if (spike_display_init(&g_ctx.disp) != ESP_OK) {
        ESP_LOGE(TAG, "display init failed - cannot measure R1");
        return;
    }

    bool nvs_ok = true;
    if (cfg->enable_wifi_load || cfg->enable_nvs_load) {
        nvs_ok = (init_nvs_for_spike() == ESP_OK);
    }

    if (cfg->enable_wifi_load) {
        if (!nvs_ok) {
            ESP_LOGW(TAG, "no NVS: skipping WiFi load");
        } else if (spike_net_connect(cfg->wifi_ssid, cfg->wifi_password) != ESP_OK) {
            ESP_LOGW(TAG, "no WiFi: R1 numbers will be OPTIMISTIC, since "
                          "WiFi DMA is a primary contender for the PSRAM bus");
        }
    }

    g_ctx.running = true;

    /* Loads on core 0, alongside WiFi. Render loop is pinned to core 1 via
     * its own task (render_task) rather than run inline here -- see the
     * comment on render_task for why that distinction is load-bearing. This
     * mirrors the task placement in docs/ARCHITECTURE.md §4 -- which is
     * itself provisional and one of the things this spike is testing. */
    xTaskCreatePinnedToCore(watermark_task, "wm", 3072, &g_ctx, 2, NULL, 0);

    if (cfg->enable_jpeg_load) {
        xTaskCreatePinnedToCore(jpeg_load_task, "jpeg", 4096, &g_ctx, 3, NULL, 0);
    }
    if (cfg->enable_nvs_load && nvs_ok) {
        xTaskCreatePinnedToCore(nvs_load_task, "nvs", 4096, &g_ctx, 2, NULL, 0);
    } else if (cfg->enable_nvs_load) {
        ESP_LOGW(TAG, "NVS load disabled because NVS did not initialise");
    }
    if (cfg->enable_tls_load) {
        xTaskCreatePinnedToCore(net_load_task, "net", 8192, &g_ctx, 5, NULL, 0);
    }

    render_ctx_t render_ctx = {
        .disp       = g_ctx.disp,
        .duration_s = cfg->duration_s,
        .done       = false,
    };
    xTaskCreatePinnedToCore(render_task, "render", 3072, &render_ctx, 5, NULL, 1);

    while (!render_ctx.done) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    g_ctx.running = false;
    vTaskDelay(pdMS_TO_TICKS(500));   /* let load tasks exit cleanly */

    uint32_t frames, vsyncs, drops, longest;
    float fps_min;
    spike_display_stats(g_ctx.disp, &frames, &vsyncs, &drops, &longest, &fps_min);

    out->duration_s      = cfg->duration_s;
    out->frames_rendered = frames;
    out->vsyncs_total    = vsyncs;
    out->frames_dropped  = drops;
    out->fps_avg         = (float)frames / (float)cfg->duration_s;
    out->fps_min         = fps_min;
    out->longest_frame_us = longest;

    out->internal_free_min = g_ctx.internal_min;
    out->psram_free_min    = g_ctx.psram_min;
    out->internal_largest_block =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    out->jpeg_decodes = g_ctx.jpeg_decodes;
    out->jpeg_decode_ms_avg = g_ctx.jpeg_decodes
        ? g_ctx.jpeg_ms_total / g_ctx.jpeg_decodes : 0;
    out->jpeg_decode_ms_max = g_ctx.jpeg_ms_max;
    out->frames_dropped_during_jpeg = g_ctx.drops_during_jpeg;

    out->nvs_writes = g_ctx.nvs_writes;
    out->nvs_write_ms_avg = g_ctx.nvs_writes
        ? g_ctx.nvs_ms_total / g_ctx.nvs_writes : 0;
    out->nvs_write_ms_max = g_ctx.nvs_ms_max;
    out->frames_dropped_during_nvs = g_ctx.drops_during_nvs;

    out->tls_heap_peak    = g_ctx.tls_heap_peak;
    out->tls_handshake_ms = g_ctx.tls_handshake_ms;
    out->bytes_downloaded = g_ctx.bytes_downloaded;
    out->download_kbps    = g_ctx.download_kbps;
}
