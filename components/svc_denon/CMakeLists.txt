# svc_denon — service layer.
#
# Skeleton scope: the MV codec only. The full service (telnet/HTTP transports,
# command coalescing, discovery) is Phase 3 and deliberately not here.
#
# REQUIRES is minimal on purpose: this is the enforcement mechanism from
# docs/ARCHITECTURE.md §1.4. The codec is pure C with no ESP-IDF dependency at
# all, which is what makes it host-testable.

idf_component_register(
    SRCS        "denon_codec.c"
    INCLUDE_DIRS "include"
    REQUIRES     ""          # nothing. Pure functions.
    PRIV_REQUIRES ""
)
