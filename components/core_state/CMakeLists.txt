# core_state — the single app_state_t snapshot store.
#
# Mutex-protected memcpy over caller-injected os_lock_if_t/time_source_t --
# no FreeRTOS dependency, directly host-testable with a no-op lock and a
# fake clock, same pattern as core_eventbus.

idf_component_register(
    SRCS        "core_state.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types"
    PRIV_REQUIRES ""
)
