# main — composition root (app layer).
#
# The app layer is the ONLY layer allowed to depend on everything, because it
# is the only place composite ownership lives (docs/ARCHITECTURE.md §1.3).
# During Phase 0 that is just the spike.

idf_component_register(
    SRCS        "main.c"
    INCLUDE_DIRS ""
    REQUIRES     spike_phase0
)
