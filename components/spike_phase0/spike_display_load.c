/*
 * Phase 0: display load. THROWAWAY. See spike_phase0.h.
 *
 * Brings up the ST7701 on the RGB bus and renders a moving gradient as fast as
 * the panel will take it, counting frames and -- the number that actually
 * matters -- frames where VSYNC arrived before the buffer was ready.
 *
 * A dropped frame here IS the R1 symptom. Tearing is what a dropped frame
 * looks like to a user.
 */

#include "spike_display_load.h"

#include <string.h>

#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "spike0.disp";

struct spike_display_s {
    esp_lcd_panel_handle_t panel;
    uint16_t              *fb[2];
    int                    active;

    volatile uint32_t frames;
    volatile uint32_t drops;
    volatile bool     buffer_ready;

    uint32_t longest_frame_us;
    int64_t  last_frame_us;

    /* rolling 1-second window, so we report worst-case fps not just average */
    uint32_t window_frames;
    int64_t  window_start_us;
    float    fps_min;
};

static spike_display_t g_disp;

/*
 * VSYNC callback. Runs in ISR context (LCD_RGB_ISR_IRAM_SAFE=y), so it must
 * stay trivial and must not call anything not in IRAM.
 *
 * If buffer_ready is false when VSYNC fires, the panel is about to scan out a
 * frame the CPU has not finished writing. That is a dropped frame.
 */
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t panel,
                               const esp_lcd_rgb_panel_event_data_t *event,
                               void *user_ctx)
{
    (void)panel;
    (void)event;
    spike_display_t *d = (spike_display_t *)user_ctx;

    if (!d->buffer_ready) {
        d->drops++;
    }
    d->buffer_ready = false;
    return false;
}

esp_err_t spike_display_init(spike_display_t **out)
{
    memset(&g_disp, 0, sizeof(g_disp));
    g_disp.fps_min = 1e9f;

    esp_lcd_rgb_panel_config_t cfg = {
        .data_width   = 16,
        .psram_trans_align = 64,
        .clk_src      = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = -1,
        .pclk_gpio_num = BSP_LCD_PCLK,
        .vsync_gpio_num = BSP_LCD_VSYNC,
        .hsync_gpio_num = BSP_LCD_HSYNC,
        .de_gpio_num   = BSP_LCD_DE,
        .data_gpio_nums = {
            BSP_LCD_B0, BSP_LCD_B1, BSP_LCD_B2, BSP_LCD_B3, BSP_LCD_B4,
            BSP_LCD_G0, BSP_LCD_G1, BSP_LCD_G2, BSP_LCD_G3, BSP_LCD_G4,
            BSP_LCD_G5,
            BSP_LCD_R0, BSP_LCD_R1, BSP_LCD_R2, BSP_LCD_R3, BSP_LCD_R4,
        },
        .timings = {
            .pclk_hz          = BSP_LCD_PIXEL_CLOCK_HZ,
            .h_res            = BSP_LCD_H_RES,
            .v_res            = BSP_LCD_V_RES,
            .hsync_pulse_width = BSP_LCD_HSYNC_PULSE,
            .hsync_back_porch  = BSP_LCD_HSYNC_BACK,
            .hsync_front_porch = BSP_LCD_HSYNC_FRONT,
            .vsync_pulse_width = BSP_LCD_VSYNC_PULSE,
            .vsync_back_porch  = BSP_LCD_VSYNC_BACK,
            .vsync_front_porch = BSP_LCD_VSYNC_FRONT,
            .flags.pclk_active_neg = true,
        },
        /* THE R1 LEVER.
         * Bounce buffers live in internal SRAM. The panel's DMA reads from
         * them instead of directly from PSRAM, so PSRAM is touched in larger,
         * more efficient bursts and contends less with WiFi.
         *
         * 480 x 20 x 2 bytes = 19.2 KB each, x2 = 38.4 KB internal.
         * Phase 0 tuning target: if frames still drop, this goes up first
         * (docs/MEMORY.md §3). */
        .bounce_buffer_size_px = BSP_LCD_H_RES * 20,
        .num_fbs = 2,
        .flags = {
            .fb_in_psram = true,
            .double_fb   = true,
        },
    };

    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &g_disp.panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_rgb_panel_event_callbacks_t cbs = { .on_vsync = on_vsync };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(
        g_disp.panel, &cbs, &g_disp));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_disp.panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_disp.panel));

    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(
        g_disp.panel, 2, (void **)&g_disp.fb[0], (void **)&g_disp.fb[1]));

    ESP_LOGI(TAG, "RGB panel up: %dx%d @ %d Hz pclk, fb0=%p fb1=%p",
             BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_LCD_PIXEL_CLOCK_HZ,
             (void *)g_disp.fb[0], (void *)g_disp.fb[1]);
    ESP_LOGI(TAG, "bounce buffers: 2 x %d px (%d bytes internal)",
             BSP_LCD_H_RES * 20, BSP_LCD_H_RES * 20 * 2 * 2);

    g_disp.window_start_us = esp_timer_get_time();
    g_disp.last_frame_us   = g_disp.window_start_us;

    *out = &g_disp;
    return ESP_OK;
}

/*
 * Render one frame of a moving gradient into the back buffer and flip.
 *
 * The gradient is deliberately a full-screen write: it forces every byte of
 * the framebuffer across the bus every frame, which is the worst case and
 * therefore the case worth measuring.
 */
void spike_display_render_frame(spike_display_t *d, uint32_t phase)
{
    uint16_t *fb = d->fb[d->active];
    int64_t   t0 = esp_timer_get_time();

    for (int y = 0; y < BSP_LCD_V_RES; y++) {
        uint16_t row = (uint16_t)((y + phase) & 0x1F);
        for (int x = 0; x < BSP_LCD_H_RES; x++) {
            uint16_t r = (uint16_t)(((x + phase) & 0x1F));
            uint16_t g = (uint16_t)(((x + y + phase) & 0x3F));
            fb[y * BSP_LCD_H_RES + x] =
                (uint16_t)((r << 11) | (g << 5) | row);
        }
    }

    d->buffer_ready = true;
    esp_lcd_panel_draw_bitmap(d->panel, 0, 0,
                              BSP_LCD_H_RES, BSP_LCD_V_RES, fb);
    d->active ^= 1;
    d->frames++;

    int64_t t1 = esp_timer_get_time();
    uint32_t dt = (uint32_t)(t1 - d->last_frame_us);
    if (dt > d->longest_frame_us) {
        d->longest_frame_us = dt;
    }
    d->last_frame_us = t1;
    (void)t0;

    /* 1-second rolling window -> worst-case fps */
    d->window_frames++;
    if (t1 - d->window_start_us >= 1000000) {
        float fps = (float)d->window_frames *
                    1000000.0f / (float)(t1 - d->window_start_us);
        if (fps < d->fps_min) {
            d->fps_min = fps;
        }
        d->window_frames  = 0;
        d->window_start_us = t1;
    }
}

void spike_display_stats(const spike_display_t *d,
                         uint32_t *frames, uint32_t *drops,
                         uint32_t *longest_us, float *fps_min)
{
    *frames     = d->frames;
    *drops      = d->drops;
    *longest_us = d->longest_frame_us;
    *fps_min    = (d->fps_min > 1e8f) ? 0.0f : d->fps_min;
}
