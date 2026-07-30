# main — composition root (app layer).
#
# The app layer is the ONLY layer allowed to depend on everything, because it
# is the only place composite ownership lives (docs/ARCHITECTURE.md §1.3).
# REQUIRES grows as app_wiring.c picks up each later phase's components.

idf_component_register(
    SRCS        "main.c" "app_start.c" "app_wiring.c"
    INCLUDE_DIRS "."
    REQUIRES     "core_types" "core_eventbus" "core_state"
                 "svc_storage" "svc_scheduler" "svc_logging" "svc_settings"
                 "svc_diagnostics" "svc_supervisor"
)
