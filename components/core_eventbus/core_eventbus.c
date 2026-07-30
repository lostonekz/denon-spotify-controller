#include "core_eventbus.h"

#include <stddef.h>

static void lock(core_eventbus_t *bus)   { bus->lock.lock(bus->lock.ctx); }
static void unlock(core_eventbus_t *bus) { bus->lock.unlock(bus->lock.ctx); }

static const topic_policy_t *find_policy(core_eventbus_t *bus, event_topic_t topic)
{
    for (uint32_t i = 0; i < bus->topic_policy_count; i++) {
        if (bus->topic_policies[i].topic == topic) {
            return &bus->topic_policies[i];
        }
    }
    return NULL;
}

/* Caller must hold bus->lock. */
static eventbus_sub_t *lookup(core_eventbus_t *bus, eventbus_sub_h h)
{
    if (h.index >= bus->sub_capacity) {
        return NULL;
    }
    eventbus_sub_t *sub = &bus->subs[h.index];
    if (!sub->in_use || sub->generation != h.generation) {
        return NULL;
    }
    return sub;
}

/* Caller must hold bus->lock. Pops every queued event through the release
 * hook (if any), leaves the ring empty. */
static void drain(eventbus_sub_t *sub)
{
    event_t dropped;
    while (core_pool_ring_pop(&sub->ring, &dropped)) {
        if (sub->policy->on_release != NULL) {
            sub->policy->on_release(&dropped, sub->policy->release_ctx);
        }
    }
}

void core_eventbus_init(core_eventbus_t *bus,
                         eventbus_sub_t *subs_storage, uint32_t sub_capacity,
                         const topic_policy_t *topic_policies, uint32_t topic_policy_count,
                         os_lock_if_t lock_if)
{
    bus->subs               = subs_storage;
    bus->sub_capacity       = sub_capacity;
    bus->topic_policies     = topic_policies;
    bus->topic_policy_count = topic_policy_count;
    bus->lock                = lock_if;

    for (uint32_t i = 0; i < sub_capacity; i++) {
        subs_storage[i].in_use     = false;
        subs_storage[i].generation = 0;
        subs_storage[i].policy     = NULL;
        subs_storage[i].drop_count = 0;
    }
}

bool core_eventbus_subscribe(core_eventbus_t *bus, event_topic_t topic,
                              event_t *queue_storage, uint32_t queue_capacity,
                              eventbus_sub_h *out)
{
    lock(bus);

    const topic_policy_t *policy = find_policy(bus, topic);
    if (policy == NULL) {
        unlock(bus);
        return false;
    }

    for (uint32_t i = 0; i < bus->sub_capacity; i++) {
        eventbus_sub_t *sub = &bus->subs[i];
        if (sub->in_use) {
            continue;
        }

        sub->in_use = true;
        sub->generation++;
        if (sub->generation == 0) {
            sub->generation = 1;   /* 0 is reserved invalid, like core_pool_slot */
        }
        sub->topic      = topic;
        sub->policy     = policy;
        sub->drop_count = 0;
        core_pool_ring_init(&sub->ring, (uint8_t *)queue_storage, sizeof(event_t), queue_capacity);

        out->index      = i;
        out->generation = sub->generation;

        unlock(bus);
        return true;
    }

    unlock(bus);
    return false;
}

void core_eventbus_unsubscribe(core_eventbus_t *bus, eventbus_sub_h sub_h)
{
    lock(bus);

    eventbus_sub_t *sub = lookup(bus, sub_h);
    if (sub == NULL) {
        unlock(bus);
        return;
    }

    drain(sub);
    sub->in_use = false;
    sub->policy = NULL;

    unlock(bus);
}

void core_eventbus_publish(core_eventbus_t *bus, const event_t *evt)
{
    lock(bus);

    for (uint32_t i = 0; i < bus->sub_capacity; i++) {
        eventbus_sub_t *sub = &bus->subs[i];
        if (!sub->in_use || sub->topic != evt->topic) {
            continue;
        }

        if (core_pool_ring_push(&sub->ring, evt)) {
            continue;
        }

        if (sub->policy->on_full == EVENT_OVERFLOW_REPLACE_OLDEST) {
            event_t oldest;
            if (core_pool_ring_pop(&sub->ring, &oldest)) {
                if (sub->policy->on_release != NULL) {
                    sub->policy->on_release(&oldest, sub->policy->release_ctx);
                }
            }
            core_pool_ring_push(&sub->ring, evt);   /* ring now has room; cannot fail */
        } else {
            if (sub->policy->on_release != NULL) {
                sub->policy->on_release(evt, sub->policy->release_ctx);
            }
        }
        sub->drop_count++;
    }

    unlock(bus);
}

bool core_eventbus_poll(core_eventbus_t *bus, eventbus_sub_h sub_h, event_t *out)
{
    lock(bus);

    eventbus_sub_t *sub = lookup(bus, sub_h);
    if (sub == NULL) {
        unlock(bus);
        return false;
    }

    bool got = core_pool_ring_pop(&sub->ring, out);

    unlock(bus);
    return got;
}

uint32_t core_eventbus_drop_count(core_eventbus_t *bus, eventbus_sub_h sub_h)
{
    lock(bus);

    eventbus_sub_t *sub = lookup(bus, sub_h);
    uint32_t count = (sub != NULL) ? sub->drop_count : 0;

    unlock(bus);
    return count;
}

void core_eventbus_shutdown(core_eventbus_t *bus)
{
    lock(bus);

    for (uint32_t i = 0; i < bus->sub_capacity; i++) {
        eventbus_sub_t *sub = &bus->subs[i];
        if (!sub->in_use) {
            continue;
        }
        drain(sub);
        sub->in_use = false;
        sub->policy = NULL;
    }

    unlock(bus);
}
