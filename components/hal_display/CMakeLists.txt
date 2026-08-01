# hal_display — layer "hal" (tools/arch_rules.json already anticipates
# this component name). Phase 5 Tier 3.

idf_component_register(
    SRCS          "hal_display.c"
    INCLUDE_DIRS  "include"
    REQUIRES      bsp_board esp_lcd_st7701 esp_lcd_panel_io_additions esp_lcd
)
