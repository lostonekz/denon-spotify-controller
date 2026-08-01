# hal_touch — layer "hal" (tools/arch_rules.json already anticipates this
# component name). Phase 5 Tier 3.

idf_component_register(
    SRCS          "hal_touch.c"
    INCLUDE_DIRS  "include"
    REQUIRES      bsp_board esp_lcd_touch_gt911 esp_lcd esp_driver_gpio esp_driver_i2c
)
