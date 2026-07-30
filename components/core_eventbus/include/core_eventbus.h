/*
 * Topic-based pub/sub event bus. docs/EVENTS_AND_STATE.md §5.
 *
 * "Each subscriber owns a queue; publishing copies the event into every
 * matching subscriber queue" (§5.1) -- so this is fan-out, not a single
 * shared queue. Each subscriber's queue is a core_pool_ring over
 * caller-owned storage; overflow policy (replace oldest vs drop newest) and
 * the release hook are per-topic, supplied by the caller at init as
 * topic_policy_t (core_types/event_types.h) and matched by topic at
 * subscribe time -- the bus itself has zero domain knowledge of what a
 * payload owns (§5.3).
 *
 * Subscription handles are generation-tagged, same mechanism as
 * core_pool_slot: a handle from before an unsubscribe does not silently
 * resolve to whatever service resubscribes into that slot next (relevant
 * because svc_supervisor's restart path tears down and recreates
 * subscriptions).
 *
 * Every public entry point takes the bus-wide os_lock_if_t for its whole
 * duration -- publish() fans out to multiple subscriber rings, which are
 * each individually single-producer/single-consumer only if every publisher
 * is serialized against every other publisher and against
 * subscribe/unsubscribe. The lock is held only across bounded ring
 * pushes/pops and the release hook; the release hook must stay as cheap and
 * non-blocking as the state-store copy in core_state -- no I/O under it.
 */
#ifndef CORE_EVENTBUS_H
#define CORE_EVENTBUS_H

#include <stdbool.h>
#include <stdint.h>

#include "core_pool_ring.h"
#include "event_types.h"
#include "os_if_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t index;
    uint32_t generation;
} eventbus_sub_h;

typedef struct {
    bool                   in_use;
    uint32_t               generation;   /* bumped on subscribe only, like core_pool_slot */
    event_topic_t          topic;
    const topic_policy_t  *policy;       /* matched from topic_policies at subscribe time */
    core_pool_ring_t       ring;         /* event_t elements, caller-owned storage */
    uint32_t               drop_count;
} eventbus_sub_t;

typedef struct {
    eventbus_sub_t        *subs;              /* caller-owned, sub_capacity entries */
    uint32_t                sub_capacity;
    const topic_policy_t   *topic_policies;   /* caller-owned, topic_policy_count entries */
    uint32_t                topic_policy_count;
    os_lock_if_t             lock;
} core_eventbus_t;

void core_eventbus_init(core_eventbus_t *bus,
                         eventbus_sub_t *subs_storage, uint32_t sub_capacity,
                         const topic_policy_t *topic_policies, uint32_t topic_policy_count,
                         os_lock_if_t lock);

/* Fails (returns false, *out untouched) if `topic` has no entry in
 * topic_policies, or every subscriber slot is in use. queue_storage must
 * hold queue_capacity event_t's; both must outlive the subscription. */
bool core_eventbus_subscribe(core_eventbus_t *bus, event_topic_t topic,
                              event_t *queue_storage, uint32_t queue_capacity,
                              eventbus_sub_h *out);

/* Drains any queued events through the topic's release hook (if any), then
 * frees the slot. No-op on an unknown or already-stale handle. */
void core_eventbus_unsubscribe(core_eventbus_t *bus, eventbus_sub_h sub);

/* Copies evt into every subscriber currently on evt->topic. Each delivery
 * is independent -- one subscriber's queue overflowing does not affect
 * delivery to any other subscriber. */
void core_eventbus_publish(core_eventbus_t *bus, const event_t *evt);

/* Pops the oldest undelivered event for this subscriber. False (out
 * untouched) if empty or the handle is stale/unknown. */
bool core_eventbus_poll(core_eventbus_t *bus, eventbus_sub_h sub, event_t *out);

/* 0 for a stale/unknown handle. */
uint32_t core_eventbus_drop_count(core_eventbus_t *bus, eventbus_sub_h sub);

/* Drains and releases every active subscriber, same as unsubscribing each
 * one -- closes the "shutdown with a non-empty queue" leak (§5.3). */
void core_eventbus_shutdown(core_eventbus_t *bus);

#ifdef __cplusplus
}
#endif

#endif /* CORE_EVENTBUS_H */
