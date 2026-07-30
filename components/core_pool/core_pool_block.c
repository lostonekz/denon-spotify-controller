#include "core_pool_block.h"

#include <string.h>

void core_pool_block_init(core_pool_block_t *p, uint8_t *storage,
                          int32_t *free_list, size_t block_size, size_t count)
{
    p->storage         = storage;
    p->free_list       = free_list;
    p->block_size      = block_size;
    p->count           = count;
    p->free_top        = count;
    p->min_free        = count;
    p->exhausted_count = 0;

    for (size_t i = 0; i < count; i++) {
        free_list[i] = (int32_t)i;
    }
}

void *core_pool_block_alloc(core_pool_block_t *p)
{
    if (p->free_top == 0) {
        p->exhausted_count++;
        return NULL;
    }

    p->free_top--;
    size_t free_now = p->free_top;
    if (free_now < p->min_free) {
        p->min_free = free_now;
    }

    int32_t idx = p->free_list[p->free_top];
    return p->storage + ((size_t)idx * p->block_size);
}

void core_pool_block_free(core_pool_block_t *p, void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    size_t offset = (size_t)((uint8_t *)ptr - p->storage);
    int32_t idx = (int32_t)(offset / p->block_size);

    p->free_list[p->free_top] = idx;
    p->free_top++;
}

size_t core_pool_block_free_count(const core_pool_block_t *p)
{
    return p->free_top;
}

size_t core_pool_block_min_free(const core_pool_block_t *p)
{
    return p->min_free;
}

uint32_t core_pool_block_exhausted_count(const core_pool_block_t *p)
{
    return p->exhausted_count;
}
