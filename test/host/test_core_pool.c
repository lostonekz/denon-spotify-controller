/*
 * Host tests for core_pool: fixed-block allocator, generation-tagged slot
 * pool, SPSC ring buffer. All three are pure index arithmetic over
 * caller-owned storage -- no FreeRTOS, no malloc, directly host-testable.
 */

#include <string.h>

#include "unity.h"

#include "core_pool_block.h"
#include "core_pool_ring.h"
#include "core_pool_slot.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- core_pool_block -------------------------------------------------- */

#define BLOCK_SIZE 8
#define BLOCK_COUNT 3

static void test_block_alloc_free_roundtrip(void)
{
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    int32_t free_list[BLOCK_COUNT];
    core_pool_block_t p;
    core_pool_block_init(&p, storage, free_list, BLOCK_SIZE, BLOCK_COUNT);

    TEST_ASSERT_EQUAL_UINT(BLOCK_COUNT, core_pool_block_free_count(&p));

    void *a = core_pool_block_alloc(&p);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_UINT(BLOCK_COUNT - 1, core_pool_block_free_count(&p));

    core_pool_block_free(&p, a);
    TEST_ASSERT_EQUAL_UINT(BLOCK_COUNT, core_pool_block_free_count(&p));
}

static void test_block_exhaustion_fails_clean_and_counts(void)
{
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    int32_t free_list[BLOCK_COUNT];
    core_pool_block_t p;
    core_pool_block_init(&p, storage, free_list, BLOCK_SIZE, BLOCK_COUNT);

    for (int i = 0; i < BLOCK_COUNT; i++) {
        TEST_ASSERT_NOT_NULL(core_pool_block_alloc(&p));
    }
    TEST_ASSERT_EQUAL_UINT(0, core_pool_block_free_count(&p));

    /* Never blocks: exhaustion is a NULL return, not a hang. Called twice
     * to prove it stays clean, not just fails once and corrupts state. */
    TEST_ASSERT_NULL(core_pool_block_alloc(&p));
    TEST_ASSERT_NULL(core_pool_block_alloc(&p));
    TEST_ASSERT_EQUAL_UINT32(2, core_pool_block_exhausted_count(&p));
}

static void test_block_min_free_is_high_water_of_usage(void)
{
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    int32_t free_list[BLOCK_COUNT];
    core_pool_block_t p;
    core_pool_block_init(&p, storage, free_list, BLOCK_SIZE, BLOCK_COUNT);

    void *a = core_pool_block_alloc(&p);
    void *b = core_pool_block_alloc(&p);
    TEST_ASSERT_EQUAL_UINT(1, core_pool_block_min_free(&p));

    core_pool_block_free(&p, a);
    core_pool_block_free(&p, b);

    /* min_free tracks the LOWEST free count ever seen, not the current one
     * -- it must not recover just because blocks were returned. */
    TEST_ASSERT_EQUAL_UINT(BLOCK_COUNT, core_pool_block_free_count(&p));
    TEST_ASSERT_EQUAL_UINT(1, core_pool_block_min_free(&p));
}

static void test_block_alloc_returns_distinct_writable_regions(void)
{
    uint8_t storage[BLOCK_SIZE * BLOCK_COUNT];
    int32_t free_list[BLOCK_COUNT];
    core_pool_block_t p;
    core_pool_block_init(&p, storage, free_list, BLOCK_SIZE, BLOCK_COUNT);

    uint8_t *a = core_pool_block_alloc(&p);
    uint8_t *b = core_pool_block_alloc(&p);
    TEST_ASSERT_NOT_EQUAL(a, b);

    memset(a, 0xAA, BLOCK_SIZE);
    memset(b, 0xBB, BLOCK_SIZE);
    TEST_ASSERT_EQUAL_UINT8(0xAA, a[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, b[0]);
}

/* ---- core_pool_slot ----------------------------------------------------- */

#define SLOT_SIZE 4
#define SLOT_COUNT 2

static void test_slot_acquire_lock_release(void)
{
    uint8_t storage[SLOT_SIZE * SLOT_COUNT];
    uint32_t generation[SLOT_COUNT];
    int32_t free_list[SLOT_COUNT];
    core_pool_slot_t p;
    core_pool_slot_init(&p, storage, generation, free_list, SLOT_SIZE, SLOT_COUNT);

    core_pool_handle_t h;
    TEST_ASSERT_TRUE(core_pool_slot_acquire(&p, &h));
    TEST_ASSERT_NOT_EQUAL(0, h.generation);   /* generation 0 is never valid */

    void *ptr = core_pool_slot_lock(&p, h);
    TEST_ASSERT_NOT_NULL(ptr);

    core_pool_slot_release(&p, h);
}

static void test_slot_exhaustion_fails_clean_and_counts(void)
{
    uint8_t storage[SLOT_SIZE * SLOT_COUNT];
    uint32_t generation[SLOT_COUNT];
    int32_t free_list[SLOT_COUNT];
    core_pool_slot_t p;
    core_pool_slot_init(&p, storage, generation, free_list, SLOT_SIZE, SLOT_COUNT);

    core_pool_handle_t h;
    TEST_ASSERT_TRUE(core_pool_slot_acquire(&p, &h));
    TEST_ASSERT_TRUE(core_pool_slot_acquire(&p, &h));

    core_pool_handle_t discard;
    TEST_ASSERT_FALSE(core_pool_slot_acquire(&p, &discard));
    TEST_ASSERT_EQUAL_UINT32(1, core_pool_slot_exhausted_count(&p));
}

static void test_slot_stale_handle_rejected_after_reacquire(void)
{
    uint8_t storage[SLOT_SIZE * SLOT_COUNT];
    uint32_t generation[SLOT_COUNT];
    int32_t free_list[SLOT_COUNT];
    core_pool_slot_t p;
    core_pool_slot_init(&p, storage, generation, free_list, SLOT_SIZE, SLOT_COUNT);

    core_pool_handle_t first;
    TEST_ASSERT_TRUE(core_pool_slot_acquire(&p, &first));
    core_pool_slot_release(&p, first);

    /* Same slot index gets reacquired for something else. */
    core_pool_handle_t second;
    TEST_ASSERT_TRUE(core_pool_slot_acquire(&p, &second));
    TEST_ASSERT_EQUAL_UINT32(first.slot, second.slot);
    TEST_ASSERT_NOT_EQUAL(first.generation, second.generation);

    /* The stale handle must not resolve to the now-reused slot's data. */
    TEST_ASSERT_NULL(core_pool_slot_lock(&p, first));
    TEST_ASSERT_NOT_NULL(core_pool_slot_lock(&p, second));
}

static void test_slot_out_of_range_handle_rejected(void)
{
    uint8_t storage[SLOT_SIZE * SLOT_COUNT];
    uint32_t generation[SLOT_COUNT];
    int32_t free_list[SLOT_COUNT];
    core_pool_slot_t p;
    core_pool_slot_init(&p, storage, generation, free_list, SLOT_SIZE, SLOT_COUNT);

    core_pool_handle_t garbage = { .slot = 99, .generation = 1 };
    TEST_ASSERT_NULL(core_pool_slot_lock(&p, garbage));
}

/* Phase 5 Tier -1: the guard that makes it safe for artwork_cache_free() to
 * hand a real, matching-generation handle to a caller after the backing
 * storage is gone (a cross-task locker racing a suspend/free) -- a matching
 * generation alone must not be read as "there's still memory here." */
static void test_slot_lock_rejects_matching_generation_when_storage_is_null(void)
{
    uint8_t storage[SLOT_SIZE * SLOT_COUNT];
    uint32_t generation[SLOT_COUNT];
    int32_t free_list[SLOT_COUNT];
    core_pool_slot_t p;
    core_pool_slot_init(&p, storage, generation, free_list, SLOT_SIZE, SLOT_COUNT);

    core_pool_handle_t h;
    TEST_ASSERT_TRUE(core_pool_slot_acquire(&p, &h));
    TEST_ASSERT_NOT_NULL(core_pool_slot_lock(&p, h));   /* valid before deinit */

    core_pool_slot_deinit(&p);

    /* Same slot, same generation -- the only thing that changed is storage. */
    TEST_ASSERT_NULL(core_pool_slot_lock(&p, h));
}

/* ---- core_pool_ring ----------------------------------------------------- */

#define RING_CAPACITY 3

static void test_ring_push_pop_fifo_order(void)
{
    uint32_t storage[RING_CAPACITY];
    core_pool_ring_t r;
    core_pool_ring_init(&r, (uint8_t *)storage, sizeof(uint32_t), RING_CAPACITY);

    uint32_t a = 1, b = 2, c = 3;
    TEST_ASSERT_TRUE(core_pool_ring_push(&r, &a));
    TEST_ASSERT_TRUE(core_pool_ring_push(&r, &b));
    TEST_ASSERT_TRUE(core_pool_ring_push(&r, &c));

    uint32_t out;
    TEST_ASSERT_TRUE(core_pool_ring_pop(&r, &out));
    TEST_ASSERT_EQUAL_UINT32(1, out);
    TEST_ASSERT_TRUE(core_pool_ring_pop(&r, &out));
    TEST_ASSERT_EQUAL_UINT32(2, out);
    TEST_ASSERT_TRUE(core_pool_ring_pop(&r, &out));
    TEST_ASSERT_EQUAL_UINT32(3, out);
}

static void test_ring_full_push_fails_without_corrupting_state(void)
{
    uint32_t storage[RING_CAPACITY];
    core_pool_ring_t r;
    core_pool_ring_init(&r, (uint8_t *)storage, sizeof(uint32_t), RING_CAPACITY);

    uint32_t v = 42;
    for (int i = 0; i < RING_CAPACITY; i++) {
        TEST_ASSERT_TRUE(core_pool_ring_push(&r, &v));
    }
    TEST_ASSERT_TRUE(core_pool_ring_is_full(&r));
    TEST_ASSERT_FALSE(core_pool_ring_push(&r, &v));
    TEST_ASSERT_EQUAL_UINT(RING_CAPACITY, core_pool_ring_count(&r));
}

static void test_ring_wraps_around_after_pop_push_cycles(void)
{
    uint32_t storage[RING_CAPACITY];
    core_pool_ring_t r;
    core_pool_ring_init(&r, (uint8_t *)storage, sizeof(uint32_t), RING_CAPACITY);

    /* Push/pop enough times that head and tail both wrap past the end of
     * the backing array at least once -- this is the case a naive index
     * (no modulo) gets wrong. */
    uint32_t out;
    for (uint32_t i = 0; i < RING_CAPACITY * 3; i++) {
        TEST_ASSERT_TRUE(core_pool_ring_push(&r, &i));
        TEST_ASSERT_TRUE(core_pool_ring_pop(&r, &out));
        TEST_ASSERT_EQUAL_UINT32(i, out);
    }
    TEST_ASSERT_TRUE(core_pool_ring_is_empty(&r));
}

static void test_ring_empty_pop_fails_without_touching_out(void)
{
    uint32_t storage[RING_CAPACITY];
    core_pool_ring_t r;
    core_pool_ring_init(&r, (uint8_t *)storage, sizeof(uint32_t), RING_CAPACITY);

    uint32_t out = 0xDEADBEEF;
    TEST_ASSERT_FALSE(core_pool_ring_pop(&r, &out));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, out);
}

static void test_ring_max_count_is_high_water(void)
{
    uint32_t storage[RING_CAPACITY];
    core_pool_ring_t r;
    core_pool_ring_init(&r, (uint8_t *)storage, sizeof(uint32_t), RING_CAPACITY);

    uint32_t v = 1, out;
    core_pool_ring_push(&r, &v);
    core_pool_ring_push(&r, &v);
    core_pool_ring_pop(&r, &out);
    core_pool_ring_pop(&r, &out);

    TEST_ASSERT_EQUAL_UINT(0, core_pool_ring_count(&r));
    TEST_ASSERT_EQUAL_UINT(2, core_pool_ring_max_count(&r));
}

/* ---- core_pool_ring bulk primitives (svc_logging's per-line writes) ---- */

static void test_ring_push_bulk_fills_only_what_fits(void)
{
    uint8_t          storage[4];
    core_pool_ring_t r;
    core_pool_ring_init(&r, storage, 1, 4);

    /* 6 offered into a capacity of 4: exactly 4 taken, oldest data never
     * silently overwritten -- push_bulk is policy-free (core_pool_ring.h). */
    TEST_ASSERT_EQUAL_UINT(4, core_pool_ring_push_bulk(&r, "ABCDEF", 6));
    TEST_ASSERT_EQUAL_UINT(4, core_pool_ring_count(&r));
    TEST_ASSERT_EQUAL_UINT(0, core_pool_ring_push_bulk(&r, "X", 1));

    uint8_t out;
    for (const char *expect = "ABCD"; *expect != '\0'; expect++) {
        TEST_ASSERT_TRUE(core_pool_ring_pop(&r, &out));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)*expect, out);
    }
}

static void test_ring_push_bulk_wraps_around_storage_end(void)
{
    uint8_t          storage[4];
    core_pool_ring_t r;
    core_pool_ring_init(&r, storage, 1, 4);

    /* Advance head to 3, leaving a 1-byte run to the end of storage so the
     * next bulk push must split into two memcpy()s. */
    core_pool_ring_push_bulk(&r, "abc", 3);
    core_pool_ring_discard(&r, 3);
    TEST_ASSERT_EQUAL_UINT(0, core_pool_ring_count(&r));

    TEST_ASSERT_EQUAL_UINT(3, core_pool_ring_push_bulk(&r, "XYZ", 3));
    uint8_t out;
    for (const char *expect = "XYZ"; *expect != '\0'; expect++) {
        TEST_ASSERT_TRUE(core_pool_ring_pop(&r, &out));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)*expect, out);
    }
}

static void test_ring_discard_drops_oldest_and_clamps(void)
{
    uint8_t          storage[4];
    core_pool_ring_t r;
    core_pool_ring_init(&r, storage, 1, 4);

    core_pool_ring_push_bulk(&r, "ABCD", 4);
    TEST_ASSERT_EQUAL_UINT(2, core_pool_ring_discard(&r, 2));

    uint8_t out;
    TEST_ASSERT_TRUE(core_pool_ring_pop(&r, &out));
    TEST_ASSERT_EQUAL_UINT8('C', out);   /* A and B were the ones dropped */

    /* Asking for more than is held drops what there is and reports that. */
    TEST_ASSERT_EQUAL_UINT(1, core_pool_ring_discard(&r, 99));
    TEST_ASSERT_TRUE(core_pool_ring_is_empty(&r));
    TEST_ASSERT_EQUAL_UINT(0, core_pool_ring_discard(&r, 1));
}

static void test_ring_push_bulk_preserves_multi_byte_elements(void)
{
    /* elem_size > 1: the byte-ring case svc_logging uses must not be the
     * only one that works -- core_eventbus rings hold whole event_t. */
    uint32_t         storage[4];
    core_pool_ring_t r;
    core_pool_ring_init(&r, (uint8_t *)storage, sizeof(uint32_t), 4);

    const uint32_t in[3] = { 111u, 222u, 333u };
    TEST_ASSERT_EQUAL_UINT(3, core_pool_ring_push_bulk(&r, in, 3));

    uint32_t out = 0;
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(core_pool_ring_pop(&r, &out));
        TEST_ASSERT_EQUAL_UINT32(in[i], out);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_block_alloc_free_roundtrip);
    RUN_TEST(test_block_exhaustion_fails_clean_and_counts);
    RUN_TEST(test_block_min_free_is_high_water_of_usage);
    RUN_TEST(test_block_alloc_returns_distinct_writable_regions);
    RUN_TEST(test_slot_acquire_lock_release);
    RUN_TEST(test_slot_exhaustion_fails_clean_and_counts);
    RUN_TEST(test_slot_stale_handle_rejected_after_reacquire);
    RUN_TEST(test_slot_out_of_range_handle_rejected);
    RUN_TEST(test_slot_lock_rejects_matching_generation_when_storage_is_null);
    RUN_TEST(test_ring_push_pop_fifo_order);
    RUN_TEST(test_ring_full_push_fails_without_corrupting_state);
    RUN_TEST(test_ring_wraps_around_after_pop_push_cycles);
    RUN_TEST(test_ring_empty_pop_fails_without_touching_out);
    RUN_TEST(test_ring_max_count_is_high_water);
    RUN_TEST(test_ring_push_bulk_fills_only_what_fits);
    RUN_TEST(test_ring_push_bulk_wraps_around_storage_end);
    RUN_TEST(test_ring_discard_drops_oldest_and_clamps);
    RUN_TEST(test_ring_push_bulk_preserves_multi_byte_elements);
    return UNITY_END();
}
