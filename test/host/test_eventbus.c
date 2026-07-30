/*
 * Host test for core_eventbus: per-topic backpressure (EVENTS_AND_STATE.md
 * §5.3), fan-out to multiple subscribers, release-hook firing on overflow /
 * unsubscribe / shutdown, drop counters, and stale-handle rejection after
 * a slot is reused.
 */

#include "unity.h"

#include "core_eventbus.h"
#include "fake_lock.h"

#define SUB_CAP   4
#define QUEUE_CAP 2

typedef struct {
    int      calls;
    event_t  last[8];
} release_capture_t;

static release_capture_t g_rel;

static void capture_release(const event_t *dropped, void *ctx)
{
    release_capture_t *cap = (release_capture_t *)ctx;
    cap->last[cap->calls] = *dropped;
    cap->calls++;
}

static topic_policy_t g_policies[3];
static eventbus_sub_t  g_subs[SUB_CAP];
static core_eventbus_t g_bus;

void setUp(void)
{
    g_rel = (release_capture_t){ 0 };

    g_policies[0] = (topic_policy_t){
        .topic = TOPIC_ARTWORK, .depth = QUEUE_CAP,
        .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = capture_release, .release_ctx = &g_rel,
    };
    g_policies[1] = (topic_policy_t){
        .topic = TOPIC_TOAST, .depth = QUEUE_CAP,
        .on_full = EVENT_OVERFLOW_DROP_NEWEST,
        .on_release = capture_release, .release_ctx = &g_rel,
    };
    g_policies[2] = (topic_policy_t){
        .topic = TOPIC_NET, .depth = QUEUE_CAP,
        .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };

    core_eventbus_init(&g_bus, g_subs, SUB_CAP, g_policies, 3, fake_lock_get());
}

void tearDown(void) {}

static event_t make_artwork_evt(uint32_t slot)
{
    event_t e = { 0 };
    e.topic = TOPIC_ARTWORK;
    e.u.artwork.kind = ARTWORK_EVT_READY;
    e.u.artwork.handle.slot = slot;
    e.u.artwork.handle.generation = 1;
    return e;
}

static event_t make_toast_evt(char c)
{
    event_t e = { 0 };
    e.topic = TOPIC_TOAST;
    e.u.toast.message[0] = c;
    return e;
}

static event_t make_net_evt(uint32_t ipv4)
{
    event_t e = { 0 };
    e.topic = TOPIC_NET;
    e.u.net.kind = NET_EVT_IP_ACQUIRED;
    e.u.net.ipv4 = ipv4;
    return e;
}

/* --- subscribe --- */

static void test_subscribe_fails_for_unknown_topic(void)
{
    static event_t storage[QUEUE_CAP];
    eventbus_sub_h h;
    bool ok = core_eventbus_subscribe(&g_bus, TOPIC_DIAG, storage, QUEUE_CAP, &h);
    TEST_ASSERT_FALSE(ok);
}

static void test_subscribe_fails_when_table_full(void)
{
    static event_t storage[SUB_CAP][QUEUE_CAP];
    eventbus_sub_h h;
    for (int i = 0; i < SUB_CAP; i++) {
        TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_ARTWORK, storage[i], QUEUE_CAP, &h));
    }
    static event_t overflow_storage[QUEUE_CAP];
    TEST_ASSERT_FALSE(core_eventbus_subscribe(&g_bus, TOPIC_ARTWORK, overflow_storage, QUEUE_CAP, &h));
}

/* --- fan-out and FIFO order --- */

static void test_publish_fans_out_to_matching_subscribers_only(void)
{
    static event_t storage_a[QUEUE_CAP], storage_b[QUEUE_CAP], storage_c[QUEUE_CAP];
    eventbus_sub_h art_a, art_b, toast_c;
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_ARTWORK, storage_a, QUEUE_CAP, &art_a));
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_ARTWORK, storage_b, QUEUE_CAP, &art_b));
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_TOAST, storage_c, QUEUE_CAP, &toast_c));

    event_t evt = make_artwork_evt(42);
    core_eventbus_publish(&g_bus, &evt);

    event_t out;
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, art_a, &out));
    TEST_ASSERT_EQUAL_UINT32(42, out.u.artwork.handle.slot);
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, art_b, &out));
    TEST_ASSERT_EQUAL_UINT32(42, out.u.artwork.handle.slot);

    TEST_ASSERT_FALSE(core_eventbus_poll(&g_bus, toast_c, &out));   /* not on this topic */
}

static void test_poll_returns_fifo_order(void)
{
    static event_t storage[QUEUE_CAP];
    eventbus_sub_h h;
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_ARTWORK, storage, QUEUE_CAP, &h));

    event_t e1 = make_artwork_evt(1);
    event_t e2 = make_artwork_evt(2);
    core_eventbus_publish(&g_bus, &e1);
    core_eventbus_publish(&g_bus, &e2);

    event_t out;
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, h, &out));
    TEST_ASSERT_EQUAL_UINT32(1, out.u.artwork.handle.slot);
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, h, &out));
    TEST_ASSERT_EQUAL_UINT32(2, out.u.artwork.handle.slot);
}

static void test_poll_on_empty_returns_false(void)
{
    static event_t storage[QUEUE_CAP];
    eventbus_sub_h h;
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_ARTWORK, storage, QUEUE_CAP, &h));

    event_t out;
    TEST_ASSERT_FALSE(core_eventbus_poll(&g_bus, h, &out));
}

/* --- backpressure: REPLACE_OLDEST --- */

static void test_replace_oldest_evicts_oldest_and_releases_it(void)
{
    static event_t storage[QUEUE_CAP];
    eventbus_sub_h h;
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_ARTWORK, storage, QUEUE_CAP, &h));

    event_t e1 = make_artwork_evt(1);
    event_t e2 = make_artwork_evt(2);
    event_t e3 = make_artwork_evt(3);   /* depth is 2 -- this one overflows */
    core_eventbus_publish(&g_bus, &e1);
    core_eventbus_publish(&g_bus, &e2);
    core_eventbus_publish(&g_bus, &e3);

    TEST_ASSERT_EQUAL(1, g_rel.calls);
    TEST_ASSERT_EQUAL_UINT32(1, g_rel.last[0].u.artwork.handle.slot);   /* the OLDEST was released */

    event_t out;
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, h, &out));
    TEST_ASSERT_EQUAL_UINT32(2, out.u.artwork.handle.slot);
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, h, &out));
    TEST_ASSERT_EQUAL_UINT32(3, out.u.artwork.handle.slot);             /* newest survived */

    TEST_ASSERT_EQUAL_UINT32(1, core_eventbus_drop_count(&g_bus, h));
}

static void test_replace_oldest_with_no_release_hook_does_not_crash(void)
{
    static event_t storage[QUEUE_CAP];
    eventbus_sub_h h;
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_NET, storage, QUEUE_CAP, &h));

    event_t e1 = make_net_evt(1);
    event_t e2 = make_net_evt(2);
    event_t e3 = make_net_evt(3);
    core_eventbus_publish(&g_bus, &e1);
    core_eventbus_publish(&g_bus, &e2);
    core_eventbus_publish(&g_bus, &e3);

    TEST_ASSERT_EQUAL_UINT32(1, core_eventbus_drop_count(&g_bus, h));
}

/* --- backpressure: DROP_NEWEST --- */

static void test_drop_newest_keeps_queue_and_releases_the_new_one(void)
{
    static event_t storage[QUEUE_CAP];
    eventbus_sub_h h;
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_TOAST, storage, QUEUE_CAP, &h));

    event_t e1 = make_toast_evt('a');
    event_t e2 = make_toast_evt('b');
    event_t e3 = make_toast_evt('c');   /* overflows; must be the one released */
    core_eventbus_publish(&g_bus, &e1);
    core_eventbus_publish(&g_bus, &e2);
    core_eventbus_publish(&g_bus, &e3);

    TEST_ASSERT_EQUAL(1, g_rel.calls);
    TEST_ASSERT_EQUAL('c', g_rel.last[0].u.toast.message[0]);

    event_t out;
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, h, &out));
    TEST_ASSERT_EQUAL('a', out.u.toast.message[0]);
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, h, &out));
    TEST_ASSERT_EQUAL('b', out.u.toast.message[0]);
    TEST_ASSERT_FALSE(core_eventbus_poll(&g_bus, h, &out));   /* 'c' never made it in */

    TEST_ASSERT_EQUAL_UINT32(1, core_eventbus_drop_count(&g_bus, h));
}

/* --- leaks named in §5.3: unsubscribe / shutdown with queued events --- */

static void test_unsubscribe_drains_queue_through_release_hook(void)
{
    static event_t storage[QUEUE_CAP];
    eventbus_sub_h h;
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_ARTWORK, storage, QUEUE_CAP, &h));

    event_t e1 = make_artwork_evt(7);
    core_eventbus_publish(&g_bus, &e1);   /* queued, never polled */

    core_eventbus_unsubscribe(&g_bus, h);

    TEST_ASSERT_EQUAL(1, g_rel.calls);
    TEST_ASSERT_EQUAL_UINT32(7, g_rel.last[0].u.artwork.handle.slot);
}

static void test_shutdown_drains_all_active_subscribers(void)
{
    static event_t storage_a[QUEUE_CAP], storage_b[QUEUE_CAP];
    eventbus_sub_h art, toast;
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_ARTWORK, storage_a, QUEUE_CAP, &art));
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_TOAST, storage_b, QUEUE_CAP, &toast));

    event_t e1 = make_artwork_evt(9);
    event_t e2 = make_toast_evt('z');
    core_eventbus_publish(&g_bus, &e1);
    core_eventbus_publish(&g_bus, &e2);

    core_eventbus_shutdown(&g_bus);

    TEST_ASSERT_EQUAL(2, g_rel.calls);
}

/* --- stale handle rejection after slot reuse --- */

static void test_stale_handle_rejected_after_resubscribe(void)
{
    static event_t storage1[QUEUE_CAP], storage2[QUEUE_CAP];
    eventbus_sub_h first, second;
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_ARTWORK, storage1, QUEUE_CAP, &first));
    core_eventbus_unsubscribe(&g_bus, first);
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_TOAST, storage2, QUEUE_CAP, &second));

    TEST_ASSERT_EQUAL_UINT32(first.index, second.index);        /* slot reused */
    TEST_ASSERT_NOT_EQUAL(first.generation, second.generation); /* but generation moved on */

    event_t out;
    TEST_ASSERT_FALSE(core_eventbus_poll(&g_bus, first, &out));       /* stale handle rejected */
    TEST_ASSERT_EQUAL_UINT32(0, core_eventbus_drop_count(&g_bus, first));

    event_t e = make_toast_evt('x');
    core_eventbus_publish(&g_bus, &e);
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, second, &out));       /* fresh handle still works */
    TEST_ASSERT_EQUAL('x', out.u.toast.message[0]);

    core_eventbus_unsubscribe(&g_bus, first);   /* no-op on stale handle, must not corrupt `second` */
    TEST_ASSERT_FALSE(core_eventbus_poll(&g_bus, second, &out));      /* already drained above; still valid, just empty */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_subscribe_fails_for_unknown_topic);
    RUN_TEST(test_subscribe_fails_when_table_full);
    RUN_TEST(test_publish_fans_out_to_matching_subscribers_only);
    RUN_TEST(test_poll_returns_fifo_order);
    RUN_TEST(test_poll_on_empty_returns_false);
    RUN_TEST(test_replace_oldest_evicts_oldest_and_releases_it);
    RUN_TEST(test_replace_oldest_with_no_release_hook_does_not_crash);
    RUN_TEST(test_drop_newest_keeps_queue_and_releases_the_new_one);
    RUN_TEST(test_unsubscribe_drains_queue_through_release_hook);
    RUN_TEST(test_shutdown_drains_all_active_subscribers);
    RUN_TEST(test_stale_handle_rejected_after_resubscribe);
    return UNITY_END();
}
