# svc_settings — typed settings + change events + write coalescing.
#
# svc_settings_migration.c is pure (kv_store_if_t only), host-tested.
# svc_settings.c is the target-only glue (a FreeRTOS mutex) around it, core_eventbus,
# and an injected kv_store_if_t -- no dependency on svc_storage or svc_scheduler
# themselves (arch_rules.json's svc layer has no self-edge).

idf_component_register(
    SRCS        "svc_settings_migration.c" "svc_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types" "core_eventbus"
    PRIV_REQUIRES ""
)
