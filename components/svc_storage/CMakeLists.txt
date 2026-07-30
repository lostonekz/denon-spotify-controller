# svc_storage — the only component that touches NVS.
#
# Target-only: nvs_open/get_blob/etc. need real flash, so this is not
# host-tested (kv_store_if.h is the seam that keeps everything built on top
# of it -- svc_settings, svc_diagnostics' fault log -- host-testable instead).

idf_component_register(
    SRCS        "svc_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES     "nvs_flash" "core_types"
    PRIV_REQUIRES ""
)
