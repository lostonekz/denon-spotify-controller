/*
 * Fixed-block allocator. docs/MEMORY.md §5, docs/ARCHITECTURE.md §6.
 *
 * Pure index arithmetic over caller-owned storage -- no malloc here, ever.
 * The caller provides the backing byte array and a scratch index array once,
 * typically at app_start(); this just manages which blocks are free. That
 * makes it directly host-testable (no FreeRTOS, no ESP-IDF) and keeps it
 * honest with the "no allocation after startup" bounded-domain philosophy:
 * this module cannot allocate even if it wanted to.
 */
#ifndef CORE_POOL_BLOCK_H
#define CORE_POOL_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  *storage;       /* block_size * count bytes, caller-owned */
    int32_t  *free_list;     /* count int32_t slots, caller-owned scratch */
    size_t    block_size;
    size_t    count;
    size_t    free_top;      /* free_list[0 .. free_top) are free block indices */
    size_t    min_free;      /* high-water: fewest free blocks ever observed */
    uint32_t  exhausted_count; /* alloc() calls that returned NULL */
} core_pool_block_t;

/* Initializes p to fully-free. storage and free_list must each stay valid
 * and be owned by nobody else for p's lifetime; free_list must hold at
 * least `count` int32_t entries. */
void core_pool_block_init(core_pool_block_t *p, uint8_t *storage,
                          int32_t *free_list, size_t block_size, size_t count);

/* Returns a pointer to a `block_size`-byte block, or NULL if exhausted --
 * never blocks, matching docs/MEMORY.md §5's "fails clean, counted, never
 * blocks" requirement. */
void *core_pool_block_alloc(core_pool_block_t *p);

/* ptr must be a value previously returned by core_pool_block_alloc(p) on
 * this same pool, not already freed. */
void core_pool_block_free(core_pool_block_t *p, void *ptr);

size_t   core_pool_block_free_count(const core_pool_block_t *p);
size_t   core_pool_block_min_free(const core_pool_block_t *p);
uint32_t core_pool_block_exhausted_count(const core_pool_block_t *p);

#ifdef __cplusplus
}
#endif

#endif /* CORE_POOL_BLOCK_H */
