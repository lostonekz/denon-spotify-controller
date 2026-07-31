# svc_artwork — service layer. docs/ROADMAP.md Phase 4.
#
# "esp_jpeg" is the espressif/esp_jpeg managed component declared in this
# component's own idf_component.yml (a real Component Registry fetch, not
# an ESP-IDF built-in) -- re-added here after the Phase 0 spike that first
# proved it out was deleted at the end of that phase.

idf_component_register(
    SRCS          "artwork_palette.c"
                  "artwork_decode.c"
                  "artwork_cache.c"
                  "artwork_service.c"
    INCLUDE_DIRS  "include"
    REQUIRES      "core_types" "core_pool" "core_eventbus" "core_state" "net_http"
    PRIV_REQUIRES "esp_jpeg" "heap"
)
