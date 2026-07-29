/*
 * Phase 0 load-generating tasks. THROWAWAY. See spike_phase0.h and
 * spike_load_tasks.h for why this is split out of spike_runner.c.
 */

#include "spike_load_tasks.h"

#include <string.h>

#include "spike_net_load.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "spike0.run";

/* Embedded via CMakeLists.txt EMBED_FILES -- a synthetic 640x640 test
 * pattern generated for this project (not sourced artwork), so there is no
 * question of embedding someone else's copyrighted image in the repo. 640
 * because that is the largest of the three sizes Spotify's image CDN
 * actually serves (640/300/64 px); see docs/MEMORY.md §1. */
extern const uint8_t test_cover_640_jpg_start[] asm("_binary_test_cover_640_jpg_start");
extern const uint8_t test_cover_640_jpg_end[]   asm("_binary_test_cover_640_jpg_end");

#define JPEG_OUT_PX     320

esp_err_t init_nvs_for_spike(void)
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
 * JPEG load. Real 640x640 decode at 1/2 scale via espressif/esp_jpeg
 * (TJpgDec) -- exactly the largest size Spotify's image CDN serves and
 * exactly the scaled-decode plan docs/MEMORY.md §1 originally described, on
 * every simulated track change.
 *
 * The ESP32-S3 has NO hardware JPEG codec (that peripheral is ESP32-P4
 * only -- checked against soc_caps.h before assuming otherwise), so this is
 * a software decode. TJpgDec supports fractional output scale natively
 * (JPEG_IMAGE_SCALE_1_2 here), which is what makes 320x320 out of a 640x640
 * source "almost free" rather than a full decode plus a separate resize.
 */
void jpeg_load_task(void *arg)
{
    spike_ctx_t *c = (spike_ctx_t *)arg;

    size_t jpg_size = (size_t)(test_cover_640_jpg_end - test_cover_640_jpg_start);

    uint16_t *dst_320 = heap_caps_malloc(
        JPEG_OUT_PX * JPEG_OUT_PX * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (dst_320 == NULL) {
        ESP_LOGE(TAG, "jpeg load: output allocation failed");
        vTaskDelete(NULL);
        return;
    }

    esp_jpeg_image_cfg_t cfg = {
        .indata      = (uint8_t *)test_cover_640_jpg_start,
        .indata_size = jpg_size,
        .outbuf      = (uint8_t *)dst_320,
        .outbuf_size = JPEG_OUT_PX * JPEG_OUT_PX * sizeof(uint16_t),
        .out_format  = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale   = JPEG_IMAGE_SCALE_1_2,
        .flags       = { .swap_color_bytes = 0 },
    };

    while (c->running) {
        /* Same before/after drop-attribution technique as nvs_load_task
         * below: sample the drop counter immediately before and after the
         * bus-heavy work, so any drop landing inside this window is
         * attributable to THIS task rather than lumped into the overall
         * rate. */
        uint32_t f0, v0, d0, l0;
        float m0;
        spike_display_stats(c->disp, &f0, &v0, &d0, &l0, &m0);

        int64_t t0 = esp_timer_get_time();

        esp_jpeg_image_output_t out;
        esp_err_t derr = esp_jpeg_decode(&cfg, &out);
        if (derr != ESP_OK) {
            ESP_LOGW(TAG, "jpeg decode: %s", esp_err_to_name(derr));
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

    heap_caps_free(dst_320);
    vTaskDelete(NULL);
}

/*
 * NVS load. A flash write stalls the SPI bus, and on some parts stalls cache
 * fills too -- which is exactly the kind of thing that drops a frame. We count
 * frames dropped specifically during writes to attribute it.
 */
void nvs_load_task(void *arg)
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

void net_load_task(void *arg)
{
    spike_ctx_t *c = (spike_ctx_t *)arg;
    spike_net_result_t r;

    /* Repeats with a cooldown between attempts -- neither of the two things
     * already tried and rejected during Phase 0 bring-up:
     *   - A tight ~2 s reconnect loop got every connection but the last
     *     dropped mid-response or 429'd; the server correctly treating that
     *     as abuse, not a real client.
     *   - A single one-shot download (the fix for the above) finishes well
     *     inside a 120 s run and leaves the rest with zero network
     *     activity -- an independent review caught that this dilutes the
     *     drop rate against a load source docs/RISKS.md R1 calls "a
     *     primary contender for the PSRAM bus". One run was even accepted
     *     with bytes_downloaded == 0 for the entire duration.
     * A bounded range request (SPIKE_NET_RANGE_BYTES) every ~5 s covers
     * most of the run with real transfer activity without reconnecting
     * often enough to trip rate limiting again. */
    while (c->running) {
        uint32_t f0, v0, d0, l0;
        float m0;
        spike_display_stats(c->disp, &f0, &v0, &d0, &l0, &m0);

        int64_t t0 = esp_timer_get_time();
        spike_net_download(c->cfg->download_url, &r);
        uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);

        uint32_t f1, v1, d1, l1;
        float m1;
        spike_display_stats(c->disp, &f1, &v1, &d1, &l1, &m1);
        (void)f0; (void)f1; (void)v0; (void)v1; (void)l0; (void)l1; (void)m0; (void)m1;

        c->net_downloads_attempted++;
        c->net_active_ms += elapsed_ms;
        c->drops_during_net += (d1 - d0);

        if (r.ok) {
            c->net_downloads_ok++;
            c->bytes_downloaded += r.bytes;
            if (r.tls_heap_peak > c->tls_heap_peak) {
                c->tls_heap_peak = r.tls_heap_peak;
            }
            if (r.handshake_ms > c->tls_handshake_ms) {
                c->tls_handshake_ms = r.handshake_ms;
            }
            c->download_kbps = r.kbps;
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}

/* Samples both heaps continuously; the minimum under combined load is the
 * number docs/MEMORY.md §1 needs. */
void watermark_task(void *arg)
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
