/*
 * GT911 touch controller (Phase 5 Tier 3, corrected Tier 7) via the real,
 * official espressif/esp_lcd_touch_gt911 driver -- not a hand-rolled I2C
 * register implementation.
 *
 * Touch reset is on the shared IO expander (CH32V003, not the CH422G an
 * earlier draft assumed) and is bundled into the SAME combined reset
 * sequence as the LCD's own reset -- bsp_io_expander_power_up_display(),
 * run once by hal_display_init() before this is ever called. There is no
 * independent touch-side reset pulse, and no INT-pin address-strapping
 * either: the real board wires no INT pin at all (confirmed against the
 * real vendor source). Address selection is a plain dual-address I2C probe
 * (0x5D then 0x14) instead.
 */
#ifndef HAL_TOUCH_H
#define HAL_TOUCH_H

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"

#include "bsp_io_expander.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t    handle;
} hal_touch_t;

/* Probes BSP_TOUCH_I2C_ADDR (0x5D) then BSP_TOUCH_I2C_ADDR_BACKUP (0x14)
 * and uses whichever one ACKs. expander is accepted for API symmetry with
 * hal_display_init()/hal_backlight_init() but not touched here -- reset is
 * already done by the time this runs (see this header's top comment).
 * i2c_bus must already be initialized and hal_display_init() must already
 * have run (bsp_board_init() must have run first, before that). */
esp_err_t hal_touch_init(hal_touch_t *touch, i2c_master_bus_handle_t i2c_bus, bsp_io_expander_t *expander);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TOUCH_H */
