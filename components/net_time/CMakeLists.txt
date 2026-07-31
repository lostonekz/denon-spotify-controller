# net_time — SNTP sync. No dedicated task (docs/ARCHITECTURE.md §4's task
# table has no row for it); driven by net_time_tick(), the Phase 1 "tick"
# pattern. Learns WiFi is up via a TOPIC_NET subscription, not a net_wifi
# dependency (the net layer has no self-edge).

idf_component_register(
    SRCS        "net_time.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types" "core_eventbus"
    PRIV_REQUIRES "esp_netif" "lwip"
)
