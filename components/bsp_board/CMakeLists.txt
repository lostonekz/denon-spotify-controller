# bsp_board — the ONLY component that knows this is a Waveshare board.
#
# Skeleton scope: bsp_pins.h only. The I2C/IO-expander/power-rail bring-up
# sequence lands in Phase 0 once the pinout is verified against hardware.
#
# Header-only for now, so INTERFACE-style registration with no SRCS.

idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES     esp_driver_gpio
)
