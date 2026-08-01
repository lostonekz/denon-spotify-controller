# hal_backlight — layer "hal" (tools/arch_rules.json already anticipates
# this component name). Phase 5 Tier 3.

idf_component_register(
    SRCS          "hal_backlight.c"
    INCLUDE_DIRS  "include"
    REQUIRES      bsp_board
)
