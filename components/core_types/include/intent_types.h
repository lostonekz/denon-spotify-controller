/*
 * Intents: UI -> services, one queue, dispatch order globally preserved.
 * docs/EVENTS_AND_STATE.md §5.1.
 *
 * Phase 1 scaffold: nothing generates or consumes a real intent yet (no UI
 * until Phase 5, no domain services with real logic until Phase 2-4). What's
 * defined here is the shape every later phase's intents fit into, not the
 * intents themselves.
 */
#ifndef CORE_INTENT_TYPES_H
#define CORE_INTENT_TYPES_H

#include <stdint.h>

#include "app_state_types.h"   /* domain_id_t, for the read-set */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INTENT_DOMAIN_PLAYBACK = 0,
    INTENT_DOMAIN_DENON,
    INTENT_DOMAIN_SETTINGS,
    INTENT_DOMAIN_POWER,
    INTENT_DOMAIN_OTA,

    /* Has no single owning service, so the dispatcher has nowhere to route
     * it except a coordinator table -- a missing table entry is a startup
     * assertion, not a runtime surprise (§5.1). Mandatory for any intent
     * whose target or effect depends on more than one domain's state; today
     * that's exactly one case, volume retargeting (adr/0007). */
    INTENT_DOMAIN_MULTI,

    INTENT_DOMAIN_COUNT,
} intent_domain_t;

/* Mechanism 1, causal sequence IDs (§5.1): the state_version of each domain
 * this intent's decision depended on. A service that finds a domain has
 * moved on since re-evaluates instead of acting on a stale premise. */
typedef struct {
    domain_id_t domain;
    uint32_t    version;
} intent_read_t;

#define INTENT_READ_SET_MAX 3
#define INTENT_PAYLOAD_MAX  16

typedef struct {
    intent_domain_t domain;
    uint16_t        kind;      /* domain-specific; defined by the owning service */
    uint32_t        seq;
    uint8_t         read_set_count;
    intent_read_t   read_set[INTENT_READ_SET_MAX];
    uint8_t         payload[INTENT_PAYLOAD_MAX];
} intent_t;

/* Mechanism 2, acknowledgements (§5.1): what the view model's pending state
 * (§6.3) reconciles against, and why a pending intent can be abandoned
 * deterministically rather than on a timeout alone. */
typedef enum {
    INTENT_ACK_ACCEPTED = 0,
    INTENT_ACK_REJECTED,
    INTENT_ACK_SUPERSEDED,
} intent_ack_kind_t;

typedef struct {
    uint32_t          seq;      /* the originating intent's seq */
    intent_ack_kind_t kind;
} intent_ack_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_INTENT_TYPES_H */
