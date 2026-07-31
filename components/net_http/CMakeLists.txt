# net_http — HTTP(S) client with an injected retry policy and a TLS
# concurrency arbiter (docs/MEMORY.md §4).
#
# net_http_retry_policy.c is pure, host-tested, mirroring ota_policy.c's
# split between decision and transport. net_http.c is the target-only
# glue (esp_http_client, a FreeRTOS counting semaphore) around it.

idf_component_register(
    SRCS        "net_http_retry_policy.c" "net_http.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types"
    PRIV_REQUIRES "esp_http_client" "mbedtls"
)
