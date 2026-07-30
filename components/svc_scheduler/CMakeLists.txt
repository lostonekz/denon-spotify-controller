# svc_scheduler — one shared worker task for deferred/periodic work.
#
# svc_scheduler_jobs.c is pure due-time logic, host-tested like core_fsm.
# svc_scheduler.c is the target-only FreeRTOS task glue around it.

idf_component_register(
    SRCS        "svc_scheduler_jobs.c" "svc_scheduler.c"
    INCLUDE_DIRS "include"
    REQUIRES     "core_types"
    PRIV_REQUIRES ""
)
