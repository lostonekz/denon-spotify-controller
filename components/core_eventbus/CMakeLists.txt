# core_eventbus — topic-based pub/sub, fan-out to per-subscriber queues.
#
# Pure logic over caller-owned storage (core_pool_ring) plus an injected
# os_lock_if_t -- no FreeRTOS dependency, directly host-testable with a
# no-op lock, same pattern as core_pool/core_fsm.

idf_component_register(
    SRCS        "core_eventbus.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types" "core_pool"
    PRIV_REQUIRES ""
)
