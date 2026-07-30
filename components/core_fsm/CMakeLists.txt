# core_fsm — table-driven FSM engine.
#
# Pure table lookup, no FreeRTOS, no domain knowledge (docs/MODULES.md §2).
# Directly host-testable, same as core_pool.

idf_component_register(
    SRCS        "core_fsm.c"
    INCLUDE_DIRS "include"
    REQUIRES     ""
    PRIV_REQUIRES ""
)
