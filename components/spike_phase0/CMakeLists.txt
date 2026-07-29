# spike_phase0 — THROWAWAY measurement harness for docs/RISKS.md R1.
#
# This component deliberately violates the layering rules (it reaches into
# everything at once) because its whole purpose is to load every subsystem
# simultaneously. tools/arch_rules.json gives it its own "spike" layer with
# broad permissions so the violation is DECLARED rather than accidental.
#
# Delete this component at the end of Phase 0.

idf_component_register(
    SRCS        "spike_report.c"
                "spike_display_load.c"
                "spike_net_load.c"
                "spike_runner.c"
    INCLUDE_DIRS "include"
    REQUIRES     bsp_board
    PRIV_REQUIRES esp_lcd esp_timer esp_wifi esp_http_client esp-tls nvs_flash
                  mbedtls esp_driver_gpio esp_driver_i2c esp_driver_ledc driver
                  esp_psram esp_hw_support
)
