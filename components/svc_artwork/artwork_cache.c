#include "artwork_cache.h"

#include <string.h>

#include "esp_heap_caps.h"

/* No mutex taken here -- callers already hold c->lock when this runs. Kept
 * as its own function so artwork_cache_lock() and artwork_cache_lock_copy()
 * share one implementation instead of the check living in two places. */
static uint16_t *cache_lock_locked(artwork_cache_t *c, artwork_handle_t handle)
{
    core_pool_handle_t h = { .slot = handle.slot, .generation = handle.generation };
    return (uint16_t *)core_pool_slot_lock(&c->pool, h);
}

bool artwork_cache_alloc(artwork_cache_t *c)
{
    if (c->lock == NULL) {
        c->lock = xSemaphoreCreateMutex();
        if (c->lock == NULL) {
            return false;
        }
    }

    uint8_t *storage = heap_caps_malloc(ARTWORK_CACHE_SLOT_COUNT * ARTWORK_CACHE_SLOT_BYTES, MALLOC_CAP_SPIRAM);
    if (storage == NULL) {
        return false;
    }

    xSemaphoreTake(c->lock, portMAX_DELAY);
    c->storage = storage;
    memset(c->generation, 0, sizeof(c->generation));
    core_pool_slot_init(&c->pool, c->storage, c->generation, c->free_list,
                          ARTWORK_CACHE_SLOT_BYTES, ARTWORK_CACHE_SLOT_COUNT);
    xSemaphoreGive(c->lock);
    return true;
}

void artwork_cache_free(artwork_cache_t *c)
{
    if (c->lock == NULL) {
        return;   /* never allocated -- nothing to protect or free */
    }

    xSemaphoreTake(c->lock, portMAX_DELAY);
    uint8_t *storage = c->storage;
    c->storage = NULL;
    core_pool_slot_deinit(&c->pool);
    xSemaphoreGive(c->lock);

    if (storage != NULL) {
        heap_caps_free(storage);
    }
}

bool artwork_cache_acquire(artwork_cache_t *c, artwork_handle_t *out_handle)
{
    if (c->lock == NULL) {
        return false;
    }
    xSemaphoreTake(c->lock, portMAX_DELAY);
    bool ok = false;
    if (c->storage != NULL) {
        core_pool_handle_t h;
        if (core_pool_slot_acquire(&c->pool, &h)) {
            out_handle->slot       = h.slot;
            out_handle->generation = h.generation;
            ok = true;
        }
    }
    xSemaphoreGive(c->lock);
    return ok;
}

void artwork_cache_release(artwork_cache_t *c, artwork_handle_t handle)
{
    if (c->lock == NULL) {
        return;
    }
    xSemaphoreTake(c->lock, portMAX_DELAY);
    if (c->storage != NULL) {
        core_pool_handle_t h = { .slot = handle.slot, .generation = handle.generation };
        core_pool_slot_release(&c->pool, h);
    }
    xSemaphoreGive(c->lock);
}

uint16_t *artwork_cache_lock(artwork_cache_t *c, artwork_handle_t handle)
{
    if (c->lock == NULL) {
        return NULL;
    }
    xSemaphoreTake(c->lock, portMAX_DELAY);
    uint16_t *p = cache_lock_locked(c, handle);
    xSemaphoreGive(c->lock);
    return p;
}

bool artwork_cache_lock_copy(artwork_cache_t *c, artwork_handle_t handle,
                               uint16_t *out_buf, size_t out_cap_bytes)
{
    if (c->lock == NULL || out_cap_bytes < ARTWORK_CACHE_SLOT_BYTES) {
        return false;
    }
    xSemaphoreTake(c->lock, portMAX_DELAY);
    uint16_t *p = cache_lock_locked(c, handle);
    if (p != NULL) {
        memcpy(out_buf, p, ARTWORK_CACHE_SLOT_BYTES);
    }
    xSemaphoreGive(c->lock);
    return p != NULL;
}

void artwork_cache_teardown(artwork_cache_t *c)
{
    artwork_cache_free(c);
    if (c->lock != NULL) {
        vSemaphoreDelete(c->lock);
        c->lock = NULL;
    }
}
