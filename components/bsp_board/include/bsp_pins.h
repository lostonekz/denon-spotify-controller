/*
 * EVERY GPIO number in this project appears exactly once, here.
 *
 * Waveshare revises these boards. When the pinout changes, one file changes.
 * docs/MODULES.md.
 *
 * Board: Waveshare ESP32-S3-Touch-LCD-4 (ESP32-S3-WROOM-1 N16R8)
 *        480x480 ST7701 on the parallel RGB bus, GT911 touch over I2C,
 *        CH422G IO expander, QMI8658 IMU.
 *
 * !! UNVERIFIED !!
 * These values are transcribed from vendor documentation and have NOT been
 * confirmed against hardware. Phase 0 step 1 is to verify every one of them
 * with a meter and the vendor schematic before anything else is trusted.
 * A wrong RGB data pin produces a plausible-looking but subtly wrong image,
 * which is a miserable thing to debug later.
 */
#ifndef BSP_PINS_H
#define BSP_PINS_H

#include "driver/gpio.h"   /* GPIO_NUM_* */

/* ---- I2C: touch, IO expander, IMU all share this bus ----
 * Kept clear of the 20 pins the RGB bus consumes. tools/check_pins.py
 * enforces that; it caught three collisions in the first draft of this file. */
#define BSP_I2C_PORT            0
#define BSP_I2C_SDA             GPIO_NUM_1
#define BSP_I2C_SCL             GPIO_NUM_2
#define BSP_I2C_FREQ_HZ         400000

/* ---- CH422G IO expander ----
 * LCD reset, touch reset and backlight enable hang off this, not off GPIOs.
 * Consequence: those are I2C transactions, not pin writes, so they are slow
 * and can fail. bsp_board owns the retry policy. */
#define BSP_IO_EXPANDER_ADDR    0x24
#define BSP_EXP_LCD_RESET       0
#define BSP_EXP_TOUCH_RESET     1
#define BSP_EXP_BACKLIGHT_EN    2
#define BSP_EXP_SD_CS           3

/* ---- GT911 capacitive touch ---- */
#define BSP_TOUCH_I2C_ADDR      0x5D    /* 0x14 on some units - probe both */
#define BSP_TOUCH_INT           GPIO_NUM_38
#define BSP_TOUCH_MAX_POINTS    5

/* ---- ST7701 panel: SPI init lines ----
 * The panel is configured over a 3-wire SPI link, then driven over RGB. */
#define BSP_LCD_SPI_CS          GPIO_NUM_39
#define BSP_LCD_SPI_SCK         GPIO_NUM_48
#define BSP_LCD_SPI_SDA         GPIO_NUM_47

/* ---- ST7701 panel: parallel RGB ----
 * 16-bit RGB565. This bus is the reason docs/RISKS.md R1 exists. */
#define BSP_LCD_H_RES           480
#define BSP_LCD_V_RES           480
#define BSP_LCD_PIXEL_CLOCK_HZ  (16 * 1000 * 1000)

#define BSP_LCD_DE              GPIO_NUM_18
#define BSP_LCD_VSYNC           GPIO_NUM_17
#define BSP_LCD_HSYNC           GPIO_NUM_16
#define BSP_LCD_PCLK            GPIO_NUM_21

#define BSP_LCD_B0              GPIO_NUM_4
#define BSP_LCD_B1              GPIO_NUM_5
#define BSP_LCD_B2              GPIO_NUM_6
#define BSP_LCD_B3              GPIO_NUM_7
#define BSP_LCD_B4              GPIO_NUM_15
#define BSP_LCD_G0              GPIO_NUM_8
#define BSP_LCD_G1              GPIO_NUM_20
#define BSP_LCD_G2              GPIO_NUM_3
#define BSP_LCD_G3              GPIO_NUM_46
#define BSP_LCD_G4              GPIO_NUM_9
#define BSP_LCD_G5              GPIO_NUM_10
#define BSP_LCD_R0              GPIO_NUM_11
#define BSP_LCD_R1              GPIO_NUM_12
#define BSP_LCD_R2              GPIO_NUM_13
#define BSP_LCD_R3              GPIO_NUM_14
#define BSP_LCD_R4              GPIO_NUM_0

/* RGB panel timing. Porch values come from the ST7701 datasheet defaults and
 * are a Phase 0 tuning target: they set the real pixel clock and therefore
 * the PSRAM bandwidth demand. */
#define BSP_LCD_HSYNC_PULSE     8
#define BSP_LCD_HSYNC_BACK      10
#define BSP_LCD_HSYNC_FRONT     20
#define BSP_LCD_VSYNC_PULSE     8
#define BSP_LCD_VSYNC_BACK      10
#define BSP_LCD_VSYNC_FRONT     20

/* ---- Backlight ---- */
#define BSP_BACKLIGHT_PWM_CH    0
#define BSP_BACKLIGHT_FREQ_HZ   20000   /* above audible */

/* ---- QMI8658 IMU (optional, Kconfig-gated) ---- */
#define BSP_IMU_I2C_ADDR        0x6B
#define BSP_IMU_INT             GPIO_NUM_40

#endif /* BSP_PINS_H */
