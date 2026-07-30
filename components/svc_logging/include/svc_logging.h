/*
 * esp_log vprintf hook -> redact -> RAM ring. docs/MODULES.md: "Install an
 * esp_log vprintf hook. Keep the last ~8 KB in a RAM ring. ... Survive into
 * the coredump." UDP syslog mirroring is a documented future addition, not
 * built here -- no net_* component exists yet for it to ride on.
 *
 * Target-only glue (esp_log_set_vprintf, a FreeRTOS mutex): the pure
 * scrubbing logic underneath is in svc_logging_redact.c, host-tested.
 */
#ifndef SVC_LOGGING_H
#define SVC_LOGGING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "core_pool_ring.h"
#include "lifecycle_types.h"
#include "svc_logging_redact.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVC_LOGGING_RING_BYTES 8192

typedef struct {
    uint8_t            ring_storage[SVC_LOGGING_RING_BYTES];
    core_pool_ring_t    ring;          /* byte ring, elem_size == 1, overwrite-on-full */
    const redact_rule_t *rules;
    size_t                rules_count;
    SemaphoreHandle_t      lock_sem;
} svc_logging_t;

/* Installs the esp_log vprintf hook. Only one svc_logging_t may be active
 * at a time -- esp_log_set_vprintf's hook signature carries no context
 * parameter, an ESP-IDF API constraint, not a design choice here. */
bool svc_logging_create(svc_logging_t *s, const redact_rule_t *rules, size_t rules_count);

/* Copies up to out_capacity bytes of the ring's current contents into out,
 * oldest byte first. Non-destructive -- does not drain the ring. Returns
 * the number of bytes copied. */
size_t svc_logging_dump(svc_logging_t *s, char *out, size_t out_capacity);

/* Not in LIFECYCLE.md §1.2's restart table -- the vprintf hook and ring
 * are tied to one static struct with no teardown/restart_fn defined, so
 * this defaults to the conservative choice, same reasoning as
 * svc_scheduler. degraded_ok = true: losing the log ring loses debugging
 * history, not correctness. */
static const service_desc_t SVC_LOGGING_SERVICE_DESC = {
    .id           = SVC_ID_LOGGING,
    .name         = "logging",
    .requires     = 0,
    .restart      = RESTART_REBOOT,
    .max_restarts = 0,
    .degraded_ok  = true,
    .teardown     = NULL,
    .restart_fn   = NULL,
    .restart_ctx  = NULL,
};

#ifdef __cplusplus
}
#endif

#endif /* SVC_LOGGING_H */
