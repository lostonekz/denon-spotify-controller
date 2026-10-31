# svc_power — inactivity timer, sole writer of DOMAIN_POWER.
#
# No hal dependency, deliberately and structurally: `svc` may not depend on
# `hal` (tools/arch_rules.json), so the backlight is actuated by
# main/app_wiring_power.c off TOPIC_POWER. That is what keeps svc_power.c
# pure core_* and host-testable (test/host/test_svc_power.c).

idf_component_register(
    SRCS        "svc_power.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types" "core_eventbus" "core_state"
    PRIV_REQUIRES ""
)
