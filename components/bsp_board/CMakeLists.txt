# bsp_board — the ONLY component that knows this is a Waveshare board.
#
# Phase 5 Tier 3: real I2C bus bring-up (bsp_board.c) and the CH422G IO
# expander driver (bsp_io_expander.c) that hal_display/hal_touch's reset
# lines and hal_backlight's enable line all go through -- bsp_pins.h's own
# comment: "LCD reset, touch reset and backlight enable hang off this, not
# off GPIOs." Preserves the Phase 0 spike's already-decided shape (R9b)
# rather than re-investigating it; real pin correctness remains Tier 7's
# job, same as bsp_pins.h's own still-open "!! UNVERIFIED !!" flag.

idf_component_register(
    SRCS          "bsp_board.c"
                  "bsp_io_expander.c"
    INCLUDE_DIRS  "include"
    REQUIRES      esp_driver_gpio esp_driver_i2c
)
