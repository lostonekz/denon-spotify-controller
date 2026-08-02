/*
 * ST7701 RGB panel bring-up via the real, official espressif/esp_lcd_st7701
 * driver (RGB-interface variant), the real CH32V003 IO-expander-driven
 * reset/power-up sequence (bsp_io_expander_power_up_display(), shared with
 * touch), and the real vendor ST7701 init command sequence -- see
 * hal_display.c's own top comment for the full real-hardware finding
 * history (Phase 5 Tier 7).
 *
 * Single framebuffer (num_fbs=1), not double-buffered: a real, confirmed
 * finding from real hardware, not the original design. LV_DISPLAY_RENDER_
 * MODE_FULL (redraw the entire 480x480 screen every cycle, straight into
 * one of 2 alternating framebuffers) put far more PSRAM bandwidth demand
 * on every single frame than the bounce-buffer DMA could reliably keep up
 * with under real WiFi/decode load, producing a periodic, regularly-timed
 * band of corrupted/blown-out color sweeping the screen (esp_lcd_rgb_
 * panel's own CONFIG_LCD_RGB_RESTART_IN_VSYNC recovering from a real
 * bounce-buffer underrun). ui_app.c uses LV_DISPLAY_RENDER_MODE_PARTIAL
 * with its own small intermediate draw buffer instead of drawing straight
 * into a full framebuffer -- most frames only touch a small, genuinely-
 * changed area, so PSRAM write bandwidth drops accordingly.
 *
 * Retried num_fbs=2 + LV_DISPLAY_RENDER_MODE_FULL later (chasing an
 * unrelated real-hardware report: the progress bar/transport/labels, redrawn
 * every UI-task tick, visibly jittering to a different vertical position
 * almost every frame while the rarely-redrawn art card stayed rock stable)
 * and confirmed it WORSE, not just reverted on suspicion: esp_lcd_panel_
 * rgb.c's own rgb_panel_draw_bitmap() (read directly) shows that with
 * bounce buffers active (this config always uses them), swapping
 * cur_fb_index happens immediately/synchronously when flush_cb calls
 * esp_lcd_panel_draw_bitmap(), with no wait for the CURRENT frame's real
 * scan-out to finish first -- the bounce-buffer refill ISR reads whichever
 * buffer is "current" at whatever row it happens to be mid-scan, so a swap
 * landing mid-frame can start pulling from a buffer LVGL only just started
 * drawing into. Real symptom: static black partial frames, up to the
 * bottom ~70% of the screen, exactly the undrawn region of a buffer caught
 * mid-draw. Fixing double-buffering correctly here would need flush_ready
 * gated on a real VSYNC boundary after the swap actually takes effect, not
 * synchronous inside flush_cb -- not attempted, reverted back to the
 * known-safe single-buffer/PARTIAL config instead. The vertical-jitter
 * report that motivated trying this is still open and still unexplained.
 */
#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "bsp_io_expander.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t    panel;
    int32_t                     h_res;   /* exposed so `ui` (layer "ui", may depend on
                                            * hal_display.h/hal_touch.h only -- tools/
                                            * arch_rules.json's ui_hal_glue exception) never
                                            * needs to reach into bsp_pins.h itself */
    int32_t                     v_res;
    volatile uint32_t          frames_rendered;   /* incremented from the RGB panel's own
                                                     * VSYNC ISR -- one real, meaningful
                                                     * counter (total frames scanned out).
                                                     * Dropped-frame counting needs real
                                                     * on-target profiling methodology this
                                                     * tier can't establish without hardware
                                                     * in hand -- Tier 7's job; stays 0 here,
                                                     * not faked. */
    volatile uint32_t          frames_dropped;
} hal_display_t;

/* expander must already be initialized (bsp_board_init() must have run
 * first). Runs the combined LCD/touch reset+power-up sequence via the
 * expander, creates the 3-wire SPI panel IO (native GPIOs --
 * BSP_LCD_SPI_CS/SCK/SDA, none of them are on the expander), creates and
 * initializes the ST7701 RGB panel at the already-decided timing, fetches
 * the single framebuffer pointer, and registers the VSYNC frame counter. */
esp_err_t hal_display_init(hal_display_t *disp, bsp_io_expander_t *expander);

/* Lockless reads of the two volatile counters above -- approximate by
 * construction (no snapshot atomicity between the two words), which is
 * fine for a diagnostics counter read at most a few times a second, the
 * same tolerance every other svc_diagnostics figure (heap, stack
 * high-water) already accepts. frames_dropped reads back 0 today (see its
 * own field comment) -- exposed anyway so the DOMAIN_UI publish site
 * doesn't need updating the day it becomes real. */
void hal_display_get_frame_counters(const hal_display_t *disp, uint32_t *out_rendered, uint32_t *out_dropped);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_H */
