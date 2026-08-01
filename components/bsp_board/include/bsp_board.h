/*
 * Real board bring-up (Phase 5 Tier 3) -- the I2C bus every other bsp_board
 * driver (bsp_io_expander.c's CH422G, hal_touch's GT911) shares.
 *
 * Preserves the Phase 0 spike's already-decided shape (docs/RISKS.md R9b:
 * "written in Phase 0 in isolation") rather than re-investigating it -- that
 * spike proved a moving gradient at 60fps and real touch coordinates on
 * this exact pinout before being deleted per arch_rules.json's own
 * "throwaway past Phase 1" rule; this is the permanent, structured version
 * of the same bring-up.
 */
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include "esp_err.h"
#include "driver/i2c_master.h"

#include "bsp_io_expander.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Recovers a wedged I2C bus (a slave holding SDA low after a reset mid-
 * transaction) before the I2C peripheral claims the pins: clocks SCL up to
 * 9 times with SDA held as input, then issues a manual STOP condition --
 * the standard bit-banged recovery sequence, done here because it must run
 * BEFORE i2c_new_master_bus() owns these GPIOs, not after. Runs
 * automatically as the first step of bsp_board_init(); exposed separately
 * so it can also be retried later if a transaction starts failing with
 * ESP_ERR_TIMEOUT after the bus is already up (a real wedge mid-runtime,
 * not just at boot). */
void bsp_board_i2c_recover(void);

/* Installs the shared I2C bus (bsp_pins.h: BSP_I2C_PORT/SDA/SCL/FREQ_HZ) and
 * the single CH32V003 IO expander instance every reset/enable/backlight
 * line goes through. The IO expander (LCD/touch reset, system power
 * enable, backlight PWM) and GT911 (touch) both hang off this one bus, per
 * bsp_pins.h's own comment. Safe to call more than once; only the first
 * call actually installs anything.
 *
 * The IO expander instance is deliberately singular and board-owned, not
 * one per caller: its output register is written as a single whole byte
 * (bsp_io_expander.h's own top comment), so hal_display/hal_touch/
 * hal_backlight each keeping an independent shadow byte would have each
 * one's write silently clobber the others' pins back to a stale state.
 * Every caller shares this one instance and its one shadow byte. */
esp_err_t bsp_board_init(void);

/* NULL if bsp_board_init() hasn't been called yet (or failed). */
i2c_master_bus_handle_t bsp_board_i2c_bus(void);

/* NULL if bsp_board_init() hasn't been called yet (or failed). */
bsp_io_expander_t *bsp_board_io_expander(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BOARD_H */
