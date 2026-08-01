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
 * bounce-buffer underrun -- RISKS.md R1's own already-documented ~7%
 * dropped/restarted-frame rate, never previously seen this visually severe
 * because it was only ever measured on Phase 0's much simpler spike code,
 * not this fully-loaded 15-service app). ui_app.c now uses LV_DISPLAY_
 * RENDER_MODE_PARTIAL with its own small intermediate draw buffer instead
 * of drawing straight into a full framebuffer -- most frames only touch a
 * small, genuinely-changed area (a moving playhead, a ticking clock), so
 * PSRAM write bandwidth drops accordingly. A second, persistent
 * framebuffer would only reintroduce exactly the staleness/tearing problem
 * a single buffer avoids for partial updates -- esp_lcd_rgb_panel's own
 * draw_bitmap() (confirmed by reading esp_lcd_panel_rgb.c directly) copies
 * a partial update into whichever framebuffer index is "current", which
 * only stays consistent frame-to-frame with exactly one buffer.
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

#ifdef __cplusplus
}
#endif

#endif /* HAL_DISPLAY_H */
