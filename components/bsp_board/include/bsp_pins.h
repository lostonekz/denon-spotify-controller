/*
 * EVERY GPIO number in this project appears exactly once, here.
 *
 * Waveshare revises these boards. When the pinout changes, one file changes.
 * docs/MODULES.md.
 *
 * Board: Waveshare ESP32-S3-Touch-LCD-4 Rev4.0 (ESP32-S3-WROOM-1 N16R8)
 *        480x480 ST7701 on the parallel RGB bus, GT911 touch over I2C,
 *        CH32V003 custom-firmware IO expander (NOT a CH422G -- an earlier
 *        draft of this file assumed CH422G from generic vendor docs; wrong).
 *
 * VERIFIED (Phase 5 Tier 7) against the real board: the previous
 * "!! UNVERIFIED !!" values in this file NACKed on real hardware (0 devices
 * found scanning the I2C bus they specified) and were replaced by reading
 * Waveshare's own official waveshareteam/ESP32-S3-Touch-LCD-4 GitHub repo
 * and its exact-pinned managed component (waveshare/esp32_s3_touch_lcd_4
 * 3.0.0) directly -- not a second guess, the real vendor source. Every value
 * below traces to that source. One conflict worth flagging for the next
 * person: the OLD (wrong) BSP_TOUCH_INT was GPIO_NUM_38, which is this
 * board's real HSYNC pin -- had the old I2C values not already NACKed first,
 * a working I2C bus would have handed HSYNC to a GPIO interrupt handler.
 */
#ifndef BSP_PINS_H
#define BSP_PINS_H

#include "driver/gpio.h"   /* GPIO_NUM_* */

/* ---- I2C: touch and the IO expander share this bus ----
 * Kept clear of the 20 pins the RGB bus consumes. tools/check_pins.py
 * enforces that; it caught three collisions in the first draft of this file. */
#define BSP_I2C_PORT            0
#define BSP_I2C_SDA             GPIO_NUM_15
#define BSP_I2C_SCL             GPIO_NUM_7
#define BSP_I2C_FREQ_HZ         400000

/* ---- CH32V003 custom IO expander ----
 * A helper MCU running Waveshare's own firmware, not a generic off-the-shelf
 * expander chip. One I2C address (0x24), register-offset protocol: every
 * transaction is a 2-byte write [register, value] (or a 1-byte write of the
 * register followed by a 1-byte read, for the read-back registers). LCD
 * reset, touch reset, system power enable and the buzzer all hang off this,
 * not off raw GPIOs -- those are I2C transactions, not pin writes, so they
 * are slow and can fail. bsp_board owns the retry policy.
 *
 * LCD_RESET and TOUCH_RESET are driven together, once, as part of a single
 * combined power-up sequence (bsp_io_expander_power_up_display()) -- the
 * real vendor code resets and releases both lines at the same time, there is
 * no independent per-driver reset pulse. RTC_INT is wired as an input on the
 * real board (an interrupt line from the expander's own RTC feature, which
 * this project does not use) and is configured as such purely so nothing
 * ever drives it as an output into whatever it's connected to. */
#define BSP_IO_EXPANDER_ADDR       0x24
#define BSP_IO_EXPANDER_REG_DIRECTION  0x02
#define BSP_IO_EXPANDER_REG_OUTPUT     0x03
#define BSP_IO_EXPANDER_REG_INPUT      0x04
#define BSP_IO_EXPANDER_REG_PWM        0x05

#define BSP_EXP_BIT_TOUCH_RESET  0x02   /* bit1 */
#define BSP_EXP_BIT_LCD_RESET    0x08   /* bit3 */
#define BSP_EXP_BIT_SYS_EN       0x20   /* bit5 -- board power enable */
#define BSP_EXP_BIT_BUZZER_EN    0x40   /* bit6 */
#define BSP_EXP_BIT_RTC_INT      0x80   /* bit7 -- input, unused */

/* All BSP_EXP_BIT_* pins as outputs except RTC_INT (input). */
#define BSP_EXP_DIRECTION       (0xFF & ~BSP_EXP_BIT_RTC_INT)

/* ---- GT911 capacitive touch ----
 * The real board wires no dedicated interrupt pin at all (confirmed: the
 * vendor's own bsp_touch_new() passes int_gpio_num = GPIO_NUM_NC) and no
 * separate reset pin (reset is the IO expander's shared LCD/touch reset
 * line, already released by the time touch initializes). Address selection
 * is a plain I2C probe, not the INT-pin address-strap trick a generic GT911
 * datasheet describes -- probe 0x5D first, then 0x14. */
#define BSP_TOUCH_I2C_ADDR         0x5D
#define BSP_TOUCH_I2C_ADDR_BACKUP  0x14
#define BSP_TOUCH_MAX_POINTS       5

/* ---- ST7701 panel: SPI init lines ----
 * The panel is configured over a 3-wire SPI link, then driven over RGB. */
#define BSP_LCD_SPI_CS          GPIO_NUM_42
#define BSP_LCD_SPI_SCK         GPIO_NUM_2
#define BSP_LCD_SPI_SDA         GPIO_NUM_1

/* ---- ST7701 panel: parallel RGB ----
 * 16-bit RGB565. This bus is the reason docs/RISKS.md R1 exists.
 *
 * GPIO 19/20 are hard-wired to the ESP32-S3's native USB D-/D+ on every unit
 * of this chip -- not configurable, not board-specific. An earlier draft put
 * BSP_LCD_G1 on GPIO 20. The board still flashed and enumerated fine (the
 * ROM bootloader owns USB before app_main runs), but the instant the app
 * configured the RGB panel, GPIO 20 was reconfigured away from USB and the
 * native USB peripheral dropped off entirely -- no error, no warning, the
 * device just vanished from Windows and stayed gone regardless of cable or
 * port. Recovering needed the board's BOOT-button download-mode sequence,
 * since normal USB was gone for good until a corrected image was flashed.
 * tools/check_pins.py now hard-fails on GPIO 19/20 for exactly this reason;
 * it only warned before, which is why this got past review once. The real
 * pin table below was double-checked against this list -- neither 19 nor 20
 * appears anywhere in it. */
#define BSP_LCD_H_RES           480
#define BSP_LCD_V_RES           480

#define BSP_LCD_DE              GPIO_NUM_40
#define BSP_LCD_VSYNC           GPIO_NUM_39
#define BSP_LCD_HSYNC           GPIO_NUM_38
#define BSP_LCD_PCLK            GPIO_NUM_41

/* DATA0-4 = B0-4, DATA5-10 = G0-5, DATA11-15 = R0-4 -- the standard RGB565
 * parallel-bus convention, and the order esp_lcd_rgb_panel_config_t's
 * data_gpio_nums[16] array is built in below. */
#define BSP_LCD_B0              GPIO_NUM_5
#define BSP_LCD_B1              GPIO_NUM_45
#define BSP_LCD_B2              GPIO_NUM_48
#define BSP_LCD_B3              GPIO_NUM_47
#define BSP_LCD_B4              GPIO_NUM_21
#define BSP_LCD_G0              GPIO_NUM_14
#define BSP_LCD_G1              GPIO_NUM_13
#define BSP_LCD_G2              GPIO_NUM_12
#define BSP_LCD_G3              GPIO_NUM_11
#define BSP_LCD_G4              GPIO_NUM_10
#define BSP_LCD_G5              GPIO_NUM_9
#define BSP_LCD_R0              GPIO_NUM_46
#define BSP_LCD_R1              GPIO_NUM_3
#define BSP_LCD_R2              GPIO_NUM_8
#define BSP_LCD_R3              GPIO_NUM_18
#define BSP_LCD_R4              GPIO_NUM_17

/* RGB panel timing -- matches esp_lcd_st7701's own
 * ST7701_480_480_PANEL_60HZ_RGB_TIMING() macro exactly (read directly from
 * managed_components/espressif__esp_lcd_st7701/include/esp_lcd_st7701.h),
 * not a second, hand-derived set of porch values. */
#define BSP_LCD_HSYNC_PULSE     10
#define BSP_LCD_HSYNC_BACK      10
#define BSP_LCD_HSYNC_FRONT     20
#define BSP_LCD_VSYNC_PULSE     10
#define BSP_LCD_VSYNC_BACK      10
#define BSP_LCD_VSYNC_FRONT     10

/* docs/RISKS.md R1 -- 30 Hz / 8 MHz, RE-CONFIRMED (performance sweep,
 * 2026-08-01) after the earlier 60 Hz reversion.
 *
 * The first 30 Hz attempt (Phase 5 Tier 7) was confirmed WORSE while
 * chasing the corner-artifact/vertical-shift symptom -- but that symptom's
 * dominant real cause (svc_diagnostics's periodic vTaskSuspendAll() freeze,
 * see svc_diagnostics.c's own top comment) was still active at the time. A
 * fixed-duration freeze is exactly the kind of confound that makes a slower
 * frame period look worse without refresh rate itself being the lever. With
 * that freeze fixed, 30 Hz was re-tested on real hardware against the real
 * Now Playing widget tree and confirmed good -- no corner artifacts, no
 * vertical shift, no visible regression versus 60 Hz.
 *
 * This does NOT re-open R1's original bandwidth question: the Phase 0
 * spike's clean three-refresh-rate test (30/50/60 Hz, run before
 * svc_diagnostics or any of the real app existed, so not subject to the
 * same freeze confound) already found drop rate is flat across refresh
 * rate -- this result is consistent with that, not a new finding. Settled
 * at 30 Hz / 8 MHz for the PSRAM-bandwidth headroom it frees up over 60 Hz,
 * with no observed visual cost. */
#define BSP_LCD_PIXEL_CLOCK_HZ  (8 * 1000 * 1000)

/* ---- Backlight ----
 * Real PWM dimming through the IO expander's REG_PWM (0-255), not a native
 * LEDC channel -- an earlier finding ("binary on/off only, no PWM-capable
 * backlight pin exists") was based on the wrong CH422G assumption and has
 * been corrected. Formula is inverted (confirmed against the real vendor
 * source): PWM value 0x00 is full brightness, 0xFF is off. */
#define BSP_BACKLIGHT_PWM_MAX   0xFF

/* No IMU on this board -- the real vendor header confirms BSP_CAPS_IMU=0.
 * An earlier draft of this file carried BSP_IMU_I2C_ADDR/BSP_IMU_INT
 * (GPIO_NUM_40, which collides with the real BSP_LCD_DE above) inherited
 * from a generic assumption; nothing in this codebase referenced them, so
 * they're removed rather than fixed. */

#endif /* BSP_PINS_H */
