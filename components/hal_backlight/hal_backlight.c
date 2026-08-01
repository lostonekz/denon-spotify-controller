#include "hal_backlight.h"

#include "bsp_pins.h"

esp_err_t hal_backlight_init(bsp_io_expander_t *expander)
{
    return hal_backlight_set_enabled(expander, true);
}

esp_err_t hal_backlight_set_enabled(bsp_io_expander_t *expander, bool on)
{
    /* Real PWM register, inverted: 0x00 is full brightness, 0xFF is off
     * (confirmed against the real vendor source -- see this component's
     * header for the full finding). Binary on/off is still all this driver
     * exposes today; Phase 6's svc_power owns real dimming. */
    return bsp_io_expander_set_pwm(expander, on ? 0x00 : BSP_BACKLIGHT_PWM_MAX);
}
