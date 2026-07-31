# svc_denon — service layer. docs/ROADMAP.md Phase 3.
#
# REQUIRES/PRIV_REQUIRES is the enforcement mechanism from
# docs/ARCHITECTURE.md §1.4 (tools/arch_rules.json: svc -> [core, net, idf]
# only, no svc->svc edge) -- listing exactly what this component actually
# needs, not what's merely available.

idf_component_register(
    SRCS          "denon_codec.c"
                  "denon_codec_tables.c"
                  "denon_cmdq.c"
                  "denon_link_fsm.c"
                  "denon_telnet.c"
                  "denon_http.c"
                  "denon_discovery.c"
                  "denon_intent.c"
                  "denon_service_state.c"
                  "denon_service.c"
                  "denon_service_link.c"
                  "denon_service_io.c"
                  "denon_service_target.c"
    INCLUDE_DIRS  "include"
    REQUIRES      "core_fsm" "core_types" "core_eventbus" "core_state" "net_http"
    PRIV_REQUIRES "lwip" "esp_timer"
)
