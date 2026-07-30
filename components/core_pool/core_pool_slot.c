#include "core_pool_slot.h"

void core_pool_slot_init(core_pool_slot_t *p, uint8_t *storage, uint32_t *generation,
                         int32_t *free_list, size_t block_size, size_t count)
{
    p->storage         = storage;
    p->generation      = generation;
    p->free_list       = free_list;
    p->block_size      = block_size;
    p->count           = count;
    p->free_top        = count;
    p->min_free        = count;
    p->exhausted_count = 0;

    for (size_t i = 0; i < count; i++) {
        free_list[i] = (int32_t)i;
        generation[i] = 0;
    }
}

bool core_pool_slot_acquire(core_pool_slot_t *p, core_pool_handle_t *out)
{
    if (p->free_top == 0) {
        p->exhausted_count++;
        return false;
    }

    p->free_top--;
    size_t free_now = p->free_top;
    if (free_now < p->min_free) {
        p->min_free = free_now;
    }

    uint32_t idx = (uint32_t)p->free_list[p->free_top];
    p->generation[idx]++;

    out->slot       = idx;
    out->generation = p->generation[idx];
    return true;
}

void core_pool_slot_release(core_pool_slot_t *p, core_pool_handle_t h)
{
    if (h.slot >= p->count) {
        return;
    }

    p->free_list[p->free_top] = (int32_t)h.slot;
    p->free_top++;
}

void *core_pool_slot_lock(const core_pool_slot_t *p, core_pool_handle_t h)
{
    if (h.slot >= p->count) {
        return NULL;
    }
    if (p->generation[h.slot] != h.generation || h.generation == 0) {
        return NULL;
    }

    return p->storage + ((size_t)h.slot * p->block_size);
}

size_t core_pool_slot_free_count(const core_pool_slot_t *p)
{
    return p->free_top;
}

size_t core_pool_slot_min_free(const core_pool_slot_t *p)
{
    return p->min_free;
}

uint32_t core_pool_slot_exhausted_count(const core_pool_slot_t *p)
{
    return p->exhausted_count;
}
