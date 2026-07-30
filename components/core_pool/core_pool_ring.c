#include "core_pool_ring.h"

#include <string.h>

void core_pool_ring_init(core_pool_ring_t *r, uint8_t *storage,
                         size_t elem_size, size_t capacity)
{
    r->storage   = storage;
    r->elem_size = elem_size;
    r->capacity  = capacity;
    r->head      = 0;
    r->tail      = 0;
    r->count     = 0;
    r->max_count = 0;
}

bool core_pool_ring_push(core_pool_ring_t *r, const void *elem)
{
    if (r->count == r->capacity) {
        return false;
    }

    memcpy(r->storage + (r->head * r->elem_size), elem, r->elem_size);
    r->head = (r->head + 1) % r->capacity;
    r->count++;
    if (r->count > r->max_count) {
        r->max_count = r->count;
    }
    return true;
}

bool core_pool_ring_pop(core_pool_ring_t *r, void *out)
{
    if (r->count == 0) {
        return false;
    }

    memcpy(out, r->storage + (r->tail * r->elem_size), r->elem_size);
    r->tail = (r->tail + 1) % r->capacity;
    r->count--;
    return true;
}

size_t core_pool_ring_count(const core_pool_ring_t *r)
{
    return r->count;
}

size_t core_pool_ring_capacity(const core_pool_ring_t *r)
{
    return r->capacity;
}

size_t core_pool_ring_max_count(const core_pool_ring_t *r)
{
    return r->max_count;
}

bool core_pool_ring_is_full(const core_pool_ring_t *r)
{
    return r->count == r->capacity;
}

bool core_pool_ring_is_empty(const core_pool_ring_t *r)
{
    return r->count == 0;
}
