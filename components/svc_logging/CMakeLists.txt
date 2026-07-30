# svc_logging — esp_log vprintf hook -> redact -> RAM ring.
#
# svc_logging_redact.c is pure scrubbing logic, host-tested like core_fsm.
# svc_logging.c is the target-only glue (esp_log_set_vprintf, a FreeRTOS
# mutex) around it and around core_pool_ring.

idf_component_register(
    SRCS        "svc_logging_redact.c" "svc_logging.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_pool" "core_types"
    PRIV_REQUIRES ""
)
