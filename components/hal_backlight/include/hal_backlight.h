/*
 * Backlight enable (Phase 5 Tier 3, corrected Tier 7). Binary on/off is
 * still all this driver exposes -- Phase 6's svc_power owns when to turn
 * it off (ASLEEP) and back on, using this same API.
 *
 * Tier 3's original finding here was wrong and is corrected: it assumed the
 * IO expander was a CH422G with only a binary enable pin, and concluded no
 * real PWM-capable backlight control existed on this board at all. The real
 * chip is a CH32V003 with a genuine PWM register (REG_PWM, 0-255) -- real
 * dimming IS possible, confirmed against the real vendor source
 * (bsp_display_brightness_set()). UI_ARCHITECTURE.md §7's "stepped
 * transition accepted, revisit only if a future hardware revision wires a
 * dedicated PWM pin" text was written against the wrong premise and needs
 * its own revisit; real percentage-based dimming is not implemented here
 * because nothing in Phase 5 needs it yet (backlight is still just
 * always-on), not because the hardware can't do it.
 */
#ifndef HAL_BACKLIGHT_H
#define HAL_BACKLIGHT_H

#include <stdbool.h>

#include "esp_err.h"

#include "bsp_io_expander.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Turns the backlight on immediately via the shared IO expander
 * (bsp_board_io_expander()). expander must already be initialized
 * (bsp_board_init() must have run first). */
esp_err_t hal_backlight_init(bsp_io_expander_t *expander);

/* Binary only -- see this file's top comment for why. */
esp_err_t hal_backlight_set_enabled(bsp_io_expander_t *expander, bool on);

#ifdef __cplusplus
}
#endif

#endif /* HAL_BACKLIGHT_H */
