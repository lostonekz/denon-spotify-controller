/*
 * See svc_power.h. Pure core_* -- no ESP-IDF, no hal, host-tested.
 */
#include "svc_power.h"

#include <string.h>

/* Which TOPIC_PLAYBACK events count as "the music changed".
 *
 * PLAYBACK_EVT_POSITION is the load-bearing exclusion: svc_spotify
 * publishes it on every poll while a track is playing, so counting it as
 * activity would mean the panel never sleeps during playback -- not
 * "stays awake while music plays" (which is the intent, and is what a
 * track change every few minutes already delivers) but "never sleeps at
 * all, including on a paused 90-minute DJ set whose position still ticks".
 * The distinction is the whole reason this is a switch and not a
 * `topic == TOPIC_PLAYBACK` test.
 *
 * SHUFFLE_REPEAT and DEVICE_CHANGED are in because they are real user
 * actions that can originate off-device (from a phone), where there is no
 * touch on this panel to notice them by. */
static bool playback_kind_is_activity(playback_evt_kind_t kind)
{
    switch (kind) {
    case PLAYBACK_EVT_TRACK_CHANGED:
    case PLAYBACK_EVT_PLAY_STATE:
    case PLAYBACK_EVT_SHUFFLE_REPEAT:
    case PLAYBACK_EVT_DEVICE_CHANGED:
        return true;
    case PLAYBACK_EVT_POSITION:
    default:
        return false;
    }
}

static void publish_and_write(svc_power_t *p)
{
    uint8_t backlight_pct = (p->mode == (uint8_t)POWER_MODE_ASLEEP) ? 0u : 100u;

    event_t evt = { .topic = TOPIC_POWER };
    evt.u.power.kind          = POWER_EVT_STATE_CHANGED;
    evt.u.power.power_state   = p->mode;
    evt.u.power.backlight_pct = backlight_pct;
    core_eventbus_publish(p->bus, &evt);

    /* Written after the publish, matching svc_denon's own publish_and_write()
     * ordering. DOMAIN_POWER has exactly one writer and this is it
     * (EVENTS_AND_STATE.md §6.1). LINK_STATUS_OK unconditionally: unlike a
     * network or AVR domain there is no remote peer to be stale against --
     * this value is locally decided and always current. */
    power_state_t out;
    memset(&out, 0, sizeof(out));
    out.power_state   = p->mode;
    out.backlight_pct = backlight_pct;
    core_state_write(p->state, DOMAIN_POWER, LINK_STATUS_OK, &out, sizeof(out));

    p->published_once = true;
}

bool svc_power_create(svc_power_t *p, const svc_power_cfg_t *cfg)
{
    memset(p, 0, sizeof(*p));
    p->state          = cfg->state;
    p->bus            = cfg->bus;
    p->clock          = cfg->clock;
    p->sleep_after_ms = cfg->sleep_after_ms;
    p->mode           = (uint8_t)POWER_MODE_ACTIVE;

    /* See the field comment in svc_power.h: seeding to "now" rather than 0
     * is what makes an untouched device stay lit for the full timeout. */
    p->last_playback_activity_us = p->clock.now_us(p->clock.ctx);

    if (!core_eventbus_subscribe(p->bus, TOPIC_PLAYBACK, p->playback_sub_storage,
                                   SVC_POWER_PLAYBACK_SUB_DEPTH, &p->playback_sub)) {
        return false;
    }
    return true;
}

void svc_power_wake(svc_power_t *p)
{
    p->last_playback_activity_us = p->clock.now_us(p->clock.ctx);
    if (p->mode != (uint8_t)POWER_MODE_ACTIVE) {
        p->mode = (uint8_t)POWER_MODE_ACTIVE;
        publish_and_write(p);
    }
}

void svc_power_tick(svc_power_t *p, uint32_t ui_idle_ms)
{
    int64_t now_us = p->clock.now_us(p->clock.ctx);

    /* Drained unconditionally, even when sleep is disabled -- an
     * unsubscribed-from-but-still-registered queue would otherwise fill and
     * start counting drops for no reason, and the drop counter is a
     * diagnostic signal this project actually reads. */
    event_t evt;
    while (core_eventbus_poll(p->bus, p->playback_sub, &evt)) {
        if (evt.topic == TOPIC_PLAYBACK && playback_kind_is_activity(evt.u.playback.kind)) {
            p->last_playback_activity_us = now_us;
        }
    }

    if (p->sleep_after_ms == 0u) {
        /* Sleep disabled: pin ACTIVE. Still publishes once so subscribers
         * get an initial state rather than silence. */
        if (!p->published_once || p->mode != (uint8_t)POWER_MODE_ACTIVE) {
            p->mode = (uint8_t)POWER_MODE_ACTIVE;
            publish_and_write(p);
        }
        return;
    }

    /* A clock that went backwards (or a not-yet-seeded timestamp) would
     * otherwise underflow this subtraction into an enormous idle time and
     * sleep the panel instantly. Clamped rather than asserted: the panel
     * staying lit is the safe failure here. */
    int64_t playback_idle_us = now_us - p->last_playback_activity_us;
    if (playback_idle_us < 0) {
        playback_idle_us = 0;
    }
    uint64_t playback_idle_ms = (uint64_t)playback_idle_us / 1000u;

    /* The panel is idle only when BOTH sources are -- so the minimum, not
     * the maximum. Touch idle comes from the caller (svc_power.h explains
     * why it is a parameter). */
    uint64_t idle_ms = playback_idle_ms;
    if ((uint64_t)ui_idle_ms < idle_ms) {
        idle_ms = (uint64_t)ui_idle_ms;
    }

    uint8_t want = (idle_ms >= (uint64_t)p->sleep_after_ms) ? (uint8_t)POWER_MODE_ASLEEP
                                                             : (uint8_t)POWER_MODE_ACTIVE;
    if (want != p->mode || !p->published_once) {
        p->mode = want;
        publish_and_write(p);
    }
}
