# svc_supervisor — lifecycle mechanism: boot graph, status tracking, restart policy.
#
# Entirely pure: svc_supervisor_graph.c and svc_supervisor_restart.c are
# index/bitmask/timestamp math with no dependencies; svc_supervisor.c itself
# needs no FreeRTOS/ESP-IDF either -- rebooting the device is injected as a
# plain function pointer (esp_restart on target, a counting fake on host),
# same seam as os_lock_if_t/time_source_t. All three are host-tested.

idf_component_register(
    SRCS        "svc_supervisor_graph.c" "svc_supervisor_restart.c" "svc_supervisor.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types"
    PRIV_REQUIRES ""
)
