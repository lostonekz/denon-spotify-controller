/*
 * Pure boot-graph validation + ordering. docs/LIFECYCLE.md §1: "Executing
 * the boot graph in topological order" is the supervisor's job; deciding
 * what that order even IS, and rejecting a cycle or a dependency on an
 * undeclared service, is pure index/bitmask math with no FreeRTOS
 * involvement -- directly host-testable, same as core_fsm.
 */
#ifndef SVC_SUPERVISOR_GRAPH_H
#define SVC_SUPERVISOR_GRAPH_H

#include <stddef.h>

#include "lifecycle_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GRAPH_OK = 0,
    GRAPH_CYCLE,               /* a dependency cycle exists among descs[] */
    GRAPH_MISSING_DEPENDENCY,  /* a descriptor requires a service_id_t not present in descs[] */
} graph_result_t;

/* Computes a topological boot order for descs[0..count). out_order (count
 * entries) receives descs[]-INDICES, not service_id_t values -- descs may
 * list services in any order and need not be indexed by id.
 *
 * GRAPH_OK: out_order is a valid dependency-respecting order (every
 * service appears after everything it requires).
 * GRAPH_CYCLE / GRAPH_MISSING_DEPENDENCY: out_order is left untouched.
 * This is a startup assertion, not a runtime decision (same principle as
 * adr/0007's mandatory-coordinator table: a missing entry is caught before
 * boot, not discovered at runtime). */
graph_result_t svc_supervisor_graph_order(const service_desc_t *descs, size_t count,
                                            size_t *out_order);

#ifdef __cplusplus
}
#endif

#endif /* SVC_SUPERVISOR_GRAPH_H */
