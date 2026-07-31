#include "artwork_cache.h"

#include <string.h>

#include "esp_heap_caps.h"

bool artwork_cache_alloc(artwork_cache_t *c)
{
    c->storage = heap_caps_malloc(ARTWORK_CACHE_SLOT_COUNT * ARTWORK_CACHE_SLOT_BYTES, MALLOC_CAP_SPIRAM);
    if (c->storage == NULL) {
        return false;
    }
    memset(c->generation, 0, sizeof(c->generation));
    core_pool_slot_init(&c->pool, c->storage, c->generation, c->free_list,
                          ARTWORK_CACHE_SLOT_BYTES, ARTWORK_CACHE_SLOT_COUNT);
    return true;
}

void artwork_cache_free(artwork_cache_t *c)
{
    if (c->storage != NULL) {
        heap_caps_free(c->storage);
        c->storage = NULL;
    }
}

bool artwork_cache_acquire(artwork_cache_t *c, artwork_handle_t *out_handle)
{
    if (c->storage == NULL) {
        return false;
    }
    core_pool_handle_t h;
    if (!core_pool_slot_acquire(&c->pool, &h)) {
        return false;
    }
    out_handle->slot       = h.slot;
    out_handle->generation = h.generation;
    return true;
}

void artwork_cache_release(artwork_cache_t *c, artwork_handle_t handle)
{
    if (c->storage == NULL) {
        return;
    }
    core_pool_handle_t h = { .slot = handle.slot, .generation = handle.generation };
    core_pool_slot_release(&c->pool, h);
}

uint16_t *artwork_cache_lock(artwork_cache_t *c, artwork_handle_t handle)
{
    if (c->storage == NULL) {
        return NULL;
    }
    core_pool_handle_t h = { .slot = handle.slot, .generation = handle.generation };
    return (uint16_t *)core_pool_slot_lock(&c->pool, h);
}
