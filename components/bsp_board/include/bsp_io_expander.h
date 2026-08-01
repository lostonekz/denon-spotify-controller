/*
 * CH32V003 custom-firmware IO expander driver (Phase 5 Tier 3, corrected
 * Tier 7) -- LCD reset, touch reset and system power-enable all hang off
 * this chip, not off raw GPIOs (bsp_pins.h's own comment).
 *
 * An earlier draft of this file was written against a CH422G (a real, but
 * WRONG, generic IO-expander chip) and NACKed on real hardware -- zero
 * devices found on a full-range I2C scan of the pins that draft specified.
 * The real chip, confirmed by reading Waveshare's own official GitHub
 * source (waveshareteam/ESP32-S3-Touch-LCD-4, managed component
 * waveshare/custom_io_expander_ch32v003 2.0.0) directly, is a CH32V003
 * RISC-V helper MCU running Waveshare's own firmware. Unlike the CH422G's
 * one-address-per-register scheme this earlier draft assumed, the real
 * protocol is the more ordinary kind: ONE I2C address (0x24), and every
 * transaction is a register-offset write (2-byte transmit: [register,
 * value]) or a register-offset read (1-byte transmit of the register,
 * followed by a 1-byte receive).
 *
 * The output register is write-only (no readback of what was last
 * written) -- a shadow byte tracks the intended state so set_output_bits()
 * can read-modify-write correctly.
 */
#ifndef BSP_IO_EXPANDER_H
#define BSP_IO_EXPANDER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_dev_handle_t dev;      /* single device @ BSP_IO_EXPANDER_ADDR */
    uint8_t                 output_shadow;
    SemaphoreHandle_t        lock;     /* guards the shadow-byte read-modify-write
                                          * across concurrent callers (LCD/touch
                                          * power-up and backlight can be driven
                                          * from different hal-layer/main tasks) */
} bsp_io_expander_t;

/* Attaches to bus (already installed by bsp_board_init()) and sets the
 * direction register (BSP_EXP_DIRECTION -- every pin output except
 * RTC_INT). Does NOT touch the output register or run the display/touch
 * power-up sequence; that's bsp_io_expander_power_up_display()'s job, run
 * once by hal_display_init() since LCD and touch reset are one shared
 * sequence, not independent per-driver pulses. Safe to call more than once;
 * only the first call actually installs anything. */
esp_err_t bsp_io_expander_init(bsp_io_expander_t *exp, i2c_master_bus_handle_t bus);

/* The real vendor power-up sequence: assert reset on LCD+touch (and hold
 * the buzzer off), wait, then release LCD+touch reset together with system
 * power enable, wait again. Ported from the real vendor's bsp_display_new()
 * (esp32_s3_touch_lcd_4.c), which runs this exact sequence once, early,
 * shared between the LCD and touch drivers -- there is no independent
 * touch-side reset pulse on the real hardware. */
esp_err_t bsp_io_expander_power_up_display(bsp_io_expander_t *exp);

/* Sets (level=true) or clears (level=false) the given output bits
 * (BSP_EXP_BIT_* from bsp_pins.h) via a read-modify-write against the
 * shadow byte, then writes REG_OUTPUT. Bounded retry (3 attempts, short
 * backoff) on a transient I2C failure -- a genuinely wedged bus needs
 * bsp_board_i2c_recover(), which this does not call itself (that's a
 * bus-wide operation, not this one device's to decide). */
esp_err_t bsp_io_expander_set_output_bits(bsp_io_expander_t *exp, uint8_t bits, bool level);

/* Real PWM backlight control (REG_PWM, 0-255). Inverted: 0x00 is full
 * brightness, 0xFF is off -- confirmed against the real vendor source
 * (bsp_display_brightness_set()'s "flipped_brightness" calculation), not
 * guessed. */
esp_err_t bsp_io_expander_set_pwm(bsp_io_expander_t *exp, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* BSP_IO_EXPANDER_H */
