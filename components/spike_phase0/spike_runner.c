/*
 * Phase 0 orchestration. THROWAWAY. See spike_phase0.h.
 *
 * Runs every load source SIMULTANEOUSLY, because that is the only
 * configuration in which R1 manifests. Measuring them one at a time produces
 * four reassuring numbers and zero information.
 *
 * The load-generating tasks themselves (JPEG, NVS, network, heap watermark)
 * live in spike_load_tasks.c -- split out purely to stay under
 * tools/check_limits.py's 400-line limit.
 */

#include "spike_phase0.h"

#include <inttypes.h>   /* PRIu32 -- see spike_report.c for why this matters */
#include <string.h>

#include "spike_display_load.h"
#include "spike_load_tasks.h"
#include "spike_net_load.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "spike0.run";

static spike_ctx_t g_ctx;

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

    bool wifi_ok = false;
    if (cfg->enable_wifi_load) {
        if (!nvs_ok) {
            ESP_LOGW(TAG, "no NVS: skipping WiFi load");
        } else if (spike_net_connect(cfg->wifi_ssid, cfg->wifi_password) != ESP_OK) {
            ESP_LOGW(TAG, "no WiFi: R1 numbers will be OPTIMISTIC, since "
                          "WiFi DMA is a primary contender for the PSRAM bus");
        } else {
            wifi_ok = true;
        }
    }

    if (wifi_ok && cfg->enable_tls_load) {
        /* Diagnostic requested after repeated unexplained download failures
         * against the usual spike target: ping + a small download from a
         * DIFFERENT host, to tell apart "this board's WiFi/TLS path is
         * broken" from "something specific to the usual target host or its
         * Range request." Runs once, blocking, before the timed load
         * starts -- adds ~15-20 s to the run, logged separately from the
         * verdict. */
        spike_net_diag();
    }

    g_ctx.running = true;

    /* Loads on core 0, alongside WiFi. Render loop is pinned to core 1 via
     * its own task (render_task) rather than run inline here -- see the
     * comment on render_task for why that distinction is load-bearing. This
     * mirrors the task placement in docs/ARCHITECTURE.md §4 -- which is
     * itself provisional and one of the things this spike is testing. */
    xTaskCreatePinnedToCore(watermark_task, "wm", 3072, &g_ctx, 2, NULL, 0);

    if (cfg->enable_jpeg_load) {
        /* Priority 1, not 3: docs/RISKS.md R1's own mitigation line says
         * "artwork decode at low priority on core 0" -- the spike had never
         * actually matched that, running JPEG above both watermark and NVS.
         * Lower priority doesn't reduce PSRAM bus contention (that's a
         * hardware arbitration question, not a FreeRTOS scheduling one),
         * but it does stop JPEG's decode loop from monopolizing core 0 CPU
         * time over WiFi's own housekeeping and the other load tasks. */
        xTaskCreatePinnedToCore(jpeg_load_task, "jpeg", 4096, &g_ctx, 1, NULL, 0);
    }
    if (cfg->enable_nvs_load && nvs_ok) {
        xTaskCreatePinnedToCore(nvs_load_task, "nvs", 4096, &g_ctx, 2, NULL, 0);
    } else if (cfg->enable_nvs_load) {
        ESP_LOGW(TAG, "NVS load disabled because NVS did not initialise");
    }
    if (cfg->enable_tls_load && wifi_ok) {
        xTaskCreatePinnedToCore(net_load_task, "net", 8192, &g_ctx, 5, NULL, 0);
    } else if (cfg->enable_tls_load) {
        ESP_LOGW(TAG, "network load disabled because WiFi did not connect");
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
    out->net_downloads_attempted   = g_ctx.net_downloads_attempted;
    out->net_downloads_ok          = g_ctx.net_downloads_ok;
    out->net_active_ms             = g_ctx.net_active_ms;
    out->frames_dropped_during_net = g_ctx.drops_during_net;

    out->wifi_load_enabled = cfg->enable_tls_load;
    out->wifi_connected    = wifi_ok;
    out->jpeg_load_enabled = cfg->enable_jpeg_load;
    out->nvs_load_enabled  = cfg->enable_nvs_load;
}
