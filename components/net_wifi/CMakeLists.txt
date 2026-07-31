# net_wifi — connection FSM, SoftAP provisioning portal, backoff, credential storage.
#
# Pure, host-tested: net_wifi_fsm.c, net_wifi_provisioning_fsm.c (both built
# on core_fsm), net_wifi_backoff.c, net_wifi_credentials.c (against an
# injected kv_store_if_t), net_wifi_ap_password.c.
# Target-only glue: net_wifi.c (the net_sup task, esp_wifi/esp_netif event
# handling), net_wifi_softap.c, net_wifi_portal_http.c, net_wifi_portal_dns.c.

idf_component_register(
    SRCS        "net_wifi_fsm.c"
                "net_wifi_provisioning_fsm.c"
                "net_wifi_backoff.c"
                "net_wifi_credentials.c"
                "net_wifi_ap_password.c"
                "net_wifi.c"
                "net_wifi_actions.c"
                "net_wifi_softap.c"
                "net_wifi_portal_http.c"
                "net_wifi_portal_dns.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types" "core_fsm" "core_eventbus" "core_state"
    PRIV_REQUIRES "esp_wifi" "esp_netif" "esp_event" "esp_http_server" "lwip"
)
