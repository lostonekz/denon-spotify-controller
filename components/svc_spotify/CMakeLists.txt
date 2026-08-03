# svc_spotify — service layer. docs/ROADMAP.md Phase 4.
#
# REQUIRES/PRIV_REQUIRES is the enforcement mechanism from
# docs/ARCHITECTURE.md §1.4 (tools/arch_rules.json: svc -> [core, net, idf]
# only, no svc->svc edge) -- listing exactly what this component actually
# needs, not what's merely available.
#
# "json" is ESP-IDF's own built-in cJSON component -- no network fetch,
# same official-IDF-component pattern as svc_denon's "console" fix in
# Phase 3. Deliberately deviates from svc_ota's hand-rolled-parser
# precedent -- see spotify_parse.h's own top comment.

idf_component_register(
    SRCS          "spotify_credentials.c"
                  "spotify_pkce_encode.c"
                  "spotify_authorize_url.c"
                  "spotify_reauth_watch.c"
                  "spotify_avr_match.c"
                  "spotify_intent.c"
                  "spotify_intent_ack.c"
                  "spotify_poll_policy.c"
                  "spotify_progress.c"
                  "spotify_session_fsm.c"
                  "spotify_parse.c"
                  "spotify_auth.c"
                  "spotify_api.c"
                  "spotify_service_state.c"
                  "spotify_service.c"
                  "spotify_service_session.c"
                  "spotify_service_task.c"
                  "spotify_service_ctrl.c"
                  "spotify_service_lifecycle.c"
    INCLUDE_DIRS  "include"
    REQUIRES      "core_fsm" "core_types" "core_eventbus" "core_state" "net_http" "net_time" "json"
    PRIV_REQUIRES "mbedtls"
)
