#include "hal_display.h"

#include "esp_attr.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7701.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_pins.h"

/* MEMORY.md §3's 20 rows (38 KiB at 480px wide, RGB565) was a real,
 * R1-tuned figure (empirically measured drop rate under real WiFi+decode
 * load), not a guess -- but real hardware (Phase 5 Tier 7) initially
 * couldn't fit it: only ~31-32 KiB of contiguous internal DMA-capable RAM
 * was free by the time hal_display_init() runs (last in app_wiring_
 * create()'s order, after WiFi/svc_denon/svc_spotify have already claimed
 * internal RAM), and esp_lcd_rgb_panel allocates its two bounce buffers
 * (RGB_LCD_PANEL_BOUNCE_BUF_NUM in esp_lcd_panel_rgb.c) as two SEPARATE
 * MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA allocations of this many bytes each
 * -- PSRAM can't satisfy that cap at all. 10 rows (19.2 KiB/buffer) was a
 * real, working fix for that budget, but undersizing this buffer has a
 * real visible cost: a smaller bounce buffer gives the DMA refill ISR less
 * slack against contention, and a real board showed exactly that -- corner
 * sparkle and a periodic few-pixel vertical shift (CONFIG_LCD_RGB_RESTART_
 * IN_VSYNC=y recovering from a real underrun, not a cosmetic-only glitch).
 * Restoring the original 20 rows is safe now, not a re-guess: fixing the
 * separate LVGL memory-pool bug (ui_app.c) also shrank
 * CONFIG_LV_MEM_SIZE_KILOBYTES from 64 to 24, a permanent .bss reservation
 * that applies here too since it's link-time, not runtime-ordered -- real
 * measurement at this exact point now shows 73 KiB free / 38.9 KiB largest
 * block, comfortably over the 19.2 KiB each of the two allocations needs. */
#define HAL_DISPLAY_BOUNCE_ROWS 20

/* The real ST7701 init sequence, ported verbatim (register bytes and
 * per-command delays) from Waveshare's own official reference
 * (waveshare/esp32_s3_touch_lcd_4 3.0.0, esp32_s3_touch_lcd_4.c). An
 * earlier draft relied on esp_lcd_st7701's own built-in default init
 * sequence (vendor_config.init_cmds = NULL) -- plausible in principle (the
 * driver ships a generic ST7701 default), but there is no evidence it was
 * ever correct for THIS specific panel variant, and it is a real, distinct
 * candidate explanation (independent of the I2C/expander pin fixes
 * elsewhere in this tier) for a backlight that lights but shows no image:
 * a generic init sequence can bring the controller out of reset without
 * ever programming the real gamma/timing/pixel-format registers this exact
 * panel needs. */
static const st7701_lcd_init_cmd_t s_st7701_init_cmds[] = {
    /* {cmd, {data}, data_size, delay_ms} */
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x21, 0x08}, 2, 0},
    {0xCD, (uint8_t[]){0x08}, 1, 0},
    {0xB0, (uint8_t[]){0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0},
    {0xB1, (uint8_t[]){0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x60}, 1, 0},
    {0xB1, (uint8_t[]){0x30}, 1, 0},
    {0xB2, (uint8_t[]){0x87}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x49}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x21}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 20},
    {0xE0, (uint8_t[]){0x00, 0x1B, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t[]){0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]){0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]){0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 16, 0},
    {0xEB, (uint8_t[]){0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 7, 0},
    {0xEC, (uint8_t[]){0x3C, 0x00}, 2, 0},
    {0xED, (uint8_t[]){0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x66}, 1, 0},
    {0x21, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

static IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel;
    (void)edata;
    hal_display_t *disp = (hal_display_t *)user_ctx;
    disp->frames_rendered++;
    return false;   /* no high-priority task woken */
}

esp_err_t hal_display_init(hal_display_t *disp, bsp_io_expander_t *expander)
{
    disp->io               = NULL;
    disp->panel             = NULL;
    disp->h_res              = BSP_LCD_H_RES;
    disp->v_res              = BSP_LCD_V_RES;
    disp->frames_rendered  = 0;
    disp->frames_dropped   = 0;

    /* LCD reset and touch reset are ONE shared sequence on the real
     * hardware (both lines come out of reset together, along with system
     * power enable) -- there is no independent per-driver reset pulse.
     * hal_touch_init() runs after this and does no reset of its own. */
    esp_err_t err = bsp_io_expander_power_up_display(expander);
    if (err != ESP_OK) {
        return err;
    }

    spi_line_config_t line_config = {
        .cs_io_type   = IO_TYPE_GPIO,
        .cs_gpio_num  = BSP_LCD_SPI_CS,
        .scl_io_type  = IO_TYPE_GPIO,
        .scl_gpio_num = BSP_LCD_SPI_SCK,
        .sda_io_type  = IO_TYPE_GPIO,
        .sda_gpio_num = BSP_LCD_SPI_SDA,
        .io_expander  = NULL,   /* all three lines are native GPIOs on this board */
    };
    esp_lcd_panel_io_3wire_spi_config_t io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
    err = esp_lcd_new_panel_io_3wire_spi(&io_config, &disp->io);
    if (err != ESP_OK) {
        return err;
    }

    esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src        = LCD_CLK_SRC_DEFAULT,
        .data_width      = 16,
        .bits_per_pixel = 16,   /* RGB565 -- CONFIG_LV_COLOR_DEPTH_16, sdkconfig.defaults */
        /* Single framebuffer, not double -- see hal_display.h's own top
         * comment for the real-hardware finding behind this (Phase 5 Tier
         * 7): with LV_DISPLAY_RENDER_MODE_PARTIAL (ui_app.c), a second
         * buffer would only reintroduce the exact staleness problem partial
         * updates need to avoid, since esp_lcd_rgb_panel's own
         * draw_bitmap() writes partial updates into whichever framebuffer
         * index is "current" (confirmed by reading esp_lcd_panel_rgb.c
         * directly) -- consistent frame-to-frame only with exactly one. */
        .num_fbs          = 1,
        .bounce_buffer_size_px = BSP_LCD_H_RES * HAL_DISPLAY_BOUNCE_ROWS,
        .dma_burst_size  = 64,
        .de_gpio_num     = BSP_LCD_DE,
        .pclk_gpio_num  = BSP_LCD_PCLK,
        .vsync_gpio_num = BSP_LCD_VSYNC,
        .hsync_gpio_num = BSP_LCD_HSYNC,
        .disp_gpio_num  = -1,   /* no dedicated DISP pin on this board */
        .data_gpio_nums = {
            BSP_LCD_B0, BSP_LCD_B1, BSP_LCD_B2, BSP_LCD_B3, BSP_LCD_B4,
            BSP_LCD_G0, BSP_LCD_G1, BSP_LCD_G2, BSP_LCD_G3, BSP_LCD_G4, BSP_LCD_G5,
            BSP_LCD_R0, BSP_LCD_R1, BSP_LCD_R2, BSP_LCD_R3, BSP_LCD_R4,
        },
        .timings = {
            .pclk_hz            = BSP_LCD_PIXEL_CLOCK_HZ,
            .h_res               = BSP_LCD_H_RES,
            .v_res               = BSP_LCD_V_RES,
            .hsync_pulse_width  = BSP_LCD_HSYNC_PULSE,
            .hsync_back_porch   = BSP_LCD_HSYNC_BACK,
            .hsync_front_porch = BSP_LCD_HSYNC_FRONT,
            .vsync_pulse_width  = BSP_LCD_VSYNC_PULSE,
            .vsync_back_porch   = BSP_LCD_VSYNC_BACK,
            .vsync_front_porch = BSP_LCD_VSYNC_FRONT,
            .flags.pclk_active_neg = false,
        },
        .flags.fb_in_psram          = 1,
        .flags.bb_invalidate_cache = 1,
    };

    st7701_vendor_config_t vendor_config = {
        .init_cmds              = s_st7701_init_cmds,
        .init_cmds_size          = sizeof(s_st7701_init_cmds) / sizeof(s_st7701_init_cmds[0]),
        .rgb_config              = &rgb_config,
        .flags.enable_io_multiplex = 0,     /* SPI init lines are dedicated pins on this
                                              * board, not shared with the RGB data bus */
    };

    esp_lcd_panel_dev_config_t panel_config = {
        /* Real vendor code passes an IO-expander pin number here
         * (BSP_LCD_RST); this driver deliberately does not -- that value is
         * an expander pin index, not a native GPIO, and reset is already
         * fully handled above via bsp_io_expander_power_up_display(). -1
         * avoids the driver trying to toggle whatever native GPIO happens
         * to share that small integer (GPIO 3 here, which this board's own
         * RGB data bus already uses as BSP_LCD_R1). */
        .reset_gpio_num = -1,
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config   = &vendor_config,
    };

    err = esp_lcd_new_panel_st7701(disp->io, &panel_config, &disp->panel);
    if (err != ESP_OK) {
        esp_lcd_panel_io_del(disp->io);
        disp->io = NULL;
        return err;
    }

    err = esp_lcd_panel_reset(disp->panel);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_lcd_panel_init(disp->panel);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_lcd_panel_disp_on_off(disp->panel, true);
    if (err != ESP_OK) {
        goto fail;
    }

    esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_vsync = on_vsync,
    };
    err = esp_lcd_rgb_panel_register_event_callbacks(disp->panel, &callbacks, disp);
    if (err != ESP_OK) {
        goto fail;
    }

    return ESP_OK;

fail:
    esp_lcd_panel_del(disp->panel);
    disp->panel = NULL;
    disp->io     = NULL;   /* ST7701 driver owns and deletes the panel IO along with itself */
    return err;
}

void hal_display_get_frame_counters(const hal_display_t *disp, uint32_t *out_rendered, uint32_t *out_dropped)
{
    if (out_rendered != NULL) {
        *out_rendered = disp->frames_rendered;
    }
    if (out_dropped != NULL) {
        *out_dropped = disp->frames_dropped;
    }
}
