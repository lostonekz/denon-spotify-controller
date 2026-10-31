/*
 * svc_power -- the inactivity threshold, on the host.
 *
 * The whole reason svc_power decides but does not actuate (svc cannot reach
 * hal -- tools/arch_rules.json) is so this file can exist: "30 minutes" is
 * exactly the kind of constant nobody wants to verify by sitting in front
 * of real hardware for half an hour.
 */
#include "unity.h"

#include <string.h>

#include "fake_lock.h"
#include "fake_time_source.h"
#include "svc_power.h"

#define MIN_MS(m) ((uint32_t)(m) * 60u * 1000u)
#define SLEEP_AFTER_MS MIN_MS(30)

static fake_time_source_t     g_fts;
static core_eventbus_t         g_bus;
static eventbus_sub_t           g_subs[4];
static topic_policy_t            g_policies[2];
static core_state_t               g_state;
static svc_power_t                 g_power;

/* A second subscriber standing in for app_wiring's TOPIC_POWER relay, so the
 * tests assert on what actually reaches the actuator rather than on
 * svc_power's private field. */
static eventbus_sub_h  g_power_sub;
static event_t          g_power_sub_storage[4];

static void setup_all(uint32_t sleep_after_ms)
{
    g_fts.now_us = 1000000;   /* deliberately not 0 -- catches any code that
                                  treats "unset" and "the epoch" alike */

    g_policies[0] = (topic_policy_t){ .topic = TOPIC_PLAYBACK, .depth = 4,
                                       .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
                                       .on_release = NULL, .release_ctx = NULL };
    g_policies[1] = (topic_policy_t){ .topic = TOPIC_POWER, .depth = 4,
                                       .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
                                       .on_release = NULL, .release_ctx = NULL };
    core_eventbus_init(&g_bus, g_subs, 4, g_policies, 2, fake_lock_get());
    core_state_init(&g_state, fake_lock_get(), fake_time_source_get(&g_fts));

    svc_power_cfg_t cfg = {
        .state          = &g_state,
        .bus             = &g_bus,
        .clock            = fake_time_source_get(&g_fts),
        .sleep_after_ms    = sleep_after_ms,
    };
    TEST_ASSERT_TRUE(svc_power_create(&g_power, &cfg));
    TEST_ASSERT_TRUE(core_eventbus_subscribe(&g_bus, TOPIC_POWER, g_power_sub_storage, 4,
                                                &g_power_sub));
}

static void advance_min(uint32_t minutes)
{
    g_fts.now_us += (int64_t)minutes * 60 * 1000000;
}

static void publish_playback(playback_evt_kind_t kind)
{
    event_t evt = { .topic = TOPIC_PLAYBACK };
    evt.u.playback.kind = kind;
    core_eventbus_publish(&g_bus, &evt);
}

/* Drains the relay's queue and returns the last power_state seen, or -1 if
 * nothing was published since the previous drain. */
static int drain_last_power_state(void)
{
    int     last = -1;
    event_t evt;
    while (core_eventbus_poll(&g_bus, g_power_sub, &evt)) {
        if (evt.topic == TOPIC_POWER && evt.u.power.kind == POWER_EVT_STATE_CHANGED) {
            last = (int)evt.u.power.power_state;
        }
    }
    return last;
}

static uint8_t state_store_power_mode(void)
{
    power_state_t ps;
    memset(&ps, 0, sizeof(ps));
    TEST_ASSERT_TRUE(core_state_read(&g_state, DOMAIN_POWER, &ps, sizeof(ps)));
    return ps.power_state;
}

void setUp(void)    { memset(&g_power, 0, sizeof(g_power)); }
void tearDown(void) { }

/* The first tick must publish even though nothing changed -- otherwise a
 * subscriber that comes up before the first transition sees nothing at all
 * and has no idea whether the panel is lit. */
static void test_first_tick_publishes_active(void)
{
    setup_all(SLEEP_AFTER_MS);
    svc_power_tick(&g_power, 0);
    TEST_ASSERT_EQUAL_INT((int)POWER_MODE_ACTIVE, drain_last_power_state());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)POWER_MODE_ACTIVE, state_store_power_mode());
    TEST_ASSERT_FALSE(svc_power_is_asleep(&g_power));
}

static void test_sleeps_after_timeout_with_no_activity(void)
{
    setup_all(SLEEP_AFTER_MS);
    svc_power_tick(&g_power, 0);
    (void)drain_last_power_state();

    advance_min(29);
    svc_power_tick(&g_power, MIN_MS(29));
    TEST_ASSERT_FALSE(svc_power_is_asleep(&g_power));
    TEST_ASSERT_EQUAL_INT(-1, drain_last_power_state());   /* no needless republish */

    advance_min(1);
    svc_power_tick(&g_power, MIN_MS(30));
    TEST_ASSERT_TRUE(svc_power_is_asleep(&g_power));
    TEST_ASSERT_EQUAL_INT((int)POWER_MODE_ASLEEP, drain_last_power_state());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)POWER_MODE_ASLEEP, state_store_power_mode());
}

/* Touch alone holds it awake even with the music long since stopped. */
static void test_touch_alone_holds_awake(void)
{
    setup_all(SLEEP_AFTER_MS);
    svc_power_tick(&g_power, 0);
    advance_min(45);
    svc_power_tick(&g_power, 0);   /* touched just now */
    TEST_ASSERT_FALSE(svc_power_is_asleep(&g_power));
}

/* ...and a track change alone holds it awake with nobody touching it. This
 * is the "stays lit while music plays" half of the feature. */
static void test_track_change_alone_holds_awake(void)
{
    setup_all(SLEEP_AFTER_MS);
    svc_power_tick(&g_power, 0);

    for (int i = 0; i < 10; i++) {
        advance_min(4);
        publish_playback(PLAYBACK_EVT_TRACK_CHANGED);
        svc_power_tick(&g_power, MIN_MS(4 * (i + 1)));   /* untouched the whole time */
        TEST_ASSERT_FALSE(svc_power_is_asleep(&g_power));
    }

    /* Music stops. 30 minutes later, and only then, the panel sleeps. */
    advance_min(30);
    svc_power_tick(&g_power, MIN_MS(70));
    TEST_ASSERT_TRUE(svc_power_is_asleep(&g_power));
}

/* The exclusion the whole design hinges on: POSITION arrives on every poll
 * during playback, so if it counted the screen could never sleep. */
static void test_position_events_are_not_activity(void)
{
    setup_all(SLEEP_AFTER_MS);
    svc_power_tick(&g_power, 0);

    for (int i = 0; i < 31; i++) {
        advance_min(1);
        publish_playback(PLAYBACK_EVT_POSITION);
        svc_power_tick(&g_power, MIN_MS(i + 1));
    }
    TEST_ASSERT_TRUE(svc_power_is_asleep(&g_power));
}

static void test_wake_resets_the_timer(void)
{
    setup_all(SLEEP_AFTER_MS);
    svc_power_tick(&g_power, 0);
    advance_min(31);
    svc_power_tick(&g_power, MIN_MS(31));
    TEST_ASSERT_TRUE(svc_power_is_asleep(&g_power));
    (void)drain_last_power_state();

    svc_power_wake(&g_power);
    TEST_ASSERT_FALSE(svc_power_is_asleep(&g_power));
    TEST_ASSERT_EQUAL_INT((int)POWER_MODE_ACTIVE, drain_last_power_state());

    /* And the clock genuinely restarted: 29 more minutes is not enough. */
    advance_min(29);
    svc_power_tick(&g_power, MIN_MS(29));
    TEST_ASSERT_FALSE(svc_power_is_asleep(&g_power));
}

static void test_zero_timeout_never_sleeps(void)
{
    setup_all(0);
    svc_power_tick(&g_power, 0);
    TEST_ASSERT_EQUAL_INT((int)POWER_MODE_ACTIVE, drain_last_power_state());

    advance_min(10000);
    svc_power_tick(&g_power, MIN_MS(10000));
    TEST_ASSERT_FALSE(svc_power_is_asleep(&g_power));
}

/* A backwards clock must not underflow into an enormous idle time and
 * blank the panel -- staying lit is the safe failure. */
static void test_backwards_clock_does_not_sleep(void)
{
    setup_all(SLEEP_AFTER_MS);
    svc_power_tick(&g_power, 0);
    g_fts.now_us -= (int64_t)60 * 60 * 1000000;   /* an hour into the past */
    svc_power_tick(&g_power, 0);
    TEST_ASSERT_FALSE(svc_power_is_asleep(&g_power));
}

/* backlight_pct must agree with the mode -- it is what the relay would use
 * if hal_backlight ever grows the real PWM path its header describes. */
static void test_backlight_pct_tracks_mode(void)
{
    setup_all(SLEEP_AFTER_MS);
    svc_power_tick(&g_power, 0);
    event_t evt;
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, g_power_sub, &evt));
    TEST_ASSERT_EQUAL_UINT8(100, evt.u.power.backlight_pct);

    advance_min(31);
    svc_power_tick(&g_power, MIN_MS(31));
    TEST_ASSERT_TRUE(core_eventbus_poll(&g_bus, g_power_sub, &evt));
    TEST_ASSERT_EQUAL_UINT8(0, evt.u.power.backlight_pct);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_tick_publishes_active);
    RUN_TEST(test_sleeps_after_timeout_with_no_activity);
    RUN_TEST(test_touch_alone_holds_awake);
    RUN_TEST(test_track_change_alone_holds_awake);
    RUN_TEST(test_position_events_are_not_activity);
    RUN_TEST(test_wake_resets_the_timer);
    RUN_TEST(test_zero_timeout_never_sleeps);
    RUN_TEST(test_backwards_clock_does_not_sleep);
    RUN_TEST(test_backlight_pct_tracks_mode);
    return UNITY_END();
}
