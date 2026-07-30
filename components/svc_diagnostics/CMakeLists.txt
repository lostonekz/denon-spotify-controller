# svc_diagnostics — health aggregation + fault/coredump retention.
#
# svc_diagnostics_faultlog.c is pure (kv_store_if_t only), host-tested.
# svc_diagnostics.c is the target-only glue (esp_core_dump, heap-caps,
# per-task stack watermarks) around it.

idf_component_register(
    SRCS        "svc_diagnostics_faultlog.c" "svc_diagnostics.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types" "core_eventbus" "espcoredump"
    PRIV_REQUIRES ""
)
