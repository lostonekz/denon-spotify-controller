# svc_ota — service layer.
#
# Skeleton scope: admission POLICY only (ota_policy.c), which is pure and
# host-tested. The transport half (esp_https_ota, flash writes, the health
# gate) is Phase 2.
#
# The policy layer has no REQUIRES because it takes an injected verifier
# rather than calling mbedTLS directly. That is what lets the wrong-key and
# rollback paths be tested deterministically with no key material.

idf_component_register(
    SRCS        "ota_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES     ""
    PRIV_REQUIRES ""
)
