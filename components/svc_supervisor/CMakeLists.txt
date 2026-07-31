# svc_supervisor — lifecycle mechanism: boot graph, status tracking, restart
# policy, and the OTA health gate.
#
# Entirely pure: svc_supervisor_graph.c, svc_supervisor_restart.c, and
# svc_supervisor_health.c are index/bitmask/timestamp math with no
# dependencies; svc_supervisor.c itself needs no FreeRTOS/ESP-IDF either --
# rebooting the device is injected as a plain function pointer (esp_restart
# on target, a counting fake on host), same seam as os_lock_if_t/
# time_source_t, and its TOPIC_NET/TOPIC_TIME subscriptions only need
# core_eventbus (a core dependency). All four are host-tested.

idf_component_register(
    SRCS        "svc_supervisor_graph.c" "svc_supervisor_restart.c"
                "svc_supervisor_health.c" "svc_supervisor.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types" "core_eventbus"
    PRIV_REQUIRES ""
)
