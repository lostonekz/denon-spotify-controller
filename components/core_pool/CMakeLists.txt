# core_pool — fixed-block allocation, slot pools, SPSC ring buffers.
#
# Pure index arithmetic over caller-owned storage, no malloc, no FreeRTOS,
# no domain knowledge (docs/MODULES.md §2). Directly host-testable, same as
# svc_denon's codec and svc_ota's policy files.

idf_component_register(
    SRCS        "core_pool_block.c"
                "core_pool_slot.c"
                "core_pool_ring.c"
    INCLUDE_DIRS "include"
    REQUIRES     ""
    PRIV_REQUIRES ""
)
