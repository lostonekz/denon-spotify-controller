/*
 * svc_power -- the inactivity timer that turns the panel off, and the only
 * writer of DOMAIN_POWER. Phase 6; the first thing to give TOPIC_POWER and
 * power_state_t a real producer (both were declared in Phase 1 and left
 * dead -- EVENTS_AND_STATE.md §5.1's own table said so).
 *
 * WHAT THIS DELIBERATELY IS NOT: it does not touch the backlight. `svc` may
 * depend only on core/net/idf (tools/arch_rules.json), so it cannot reach
 * hal_backlight even in principle. It decides, publishes TOPIC_POWER, and
 * main/app_wiring_power.c actuates -- the same mediator-trampoline shape
 * app_wiring already uses for the health gate, the Denon settings relay and
 * the OTA/artwork relay. The payoff is that everything below is pure
 * core_* and host-testable (test/host/test_svc_power.c), which the
 * threshold arithmetic genuinely benefits from: "30 minutes" is exactly the
 * kind of constant nobody wants to verify on real hardware.
 *
 * No core_fsm table: ACTIVE/ASLEEP is two states, under §6.4's ">3 states"
 * bar, and a two-row transition table would be more machinery than the
 * comparison it wraps.
 */
#ifndef SVC_POWER_H
#define SVC_POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state_types.h"
#include "core_eventbus.h"
#include "core_state.h"
#include "event_types.h"
#include "os_if_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Depth 4 matches TOPIC_PLAYBACK's own policy (EVENTS_AND_STATE.md §5.3).
 * Overflow is harmless here in a way it is not for other subscribers: this
 * service only asks "did anything happen", so a replaced-oldest event still
 * leaves a newer one behind carrying the same answer. */
#define SVC_POWER_PLAYBACK_SUB_DEPTH 4

typedef struct {
    core_state_t    *state;
    core_eventbus_t *bus;
    time_source_t    clock;
    /* 0 disables sleep entirely and pins the panel ACTIVE -- the honest
     * representation of CONFIG_DSC_SCREEN_SLEEP_AFTER_MIN=0, rather than a
     * separate enable flag that could disagree with the timeout. */
    uint32_t         sleep_after_ms;
} svc_power_cfg_t;

typedef struct {
    core_state_t    *state;
    core_eventbus_t *bus;
    time_source_t    clock;
    uint32_t         sleep_after_ms;

    eventbus_sub_h   playback_sub;
    event_t          playback_sub_storage[SVC_POWER_PLAYBACK_SUB_DEPTH];

    /* Seeded to creation time, not 0: a device that boots and is never
     * touched should stay lit for the full timeout, not decide it has
     * already been idle since the epoch. */
    int64_t          last_playback_activity_us;
    uint8_t          mode;             /* a power_mode_t value */
    bool             published_once;   /* forces the first tick to publish, so
                                          subscribers get an initial ACTIVE
                                          rather than nothing until the first
                                          transition */
} svc_power_t;

/* Subscribes to TOPIC_PLAYBACK. False if that subscription fails (no policy
 * entry, or the bus is out of subscriber slots) -- boot-fatal at the call
 * site, same as every other subscribe in this project. */
bool svc_power_create(svc_power_t *p, const svc_power_cfg_t *cfg);

/* Drains TOPIC_PLAYBACK, folds in the caller-supplied touch idle time, and
 * publishes + writes DOMAIN_POWER when the mode changes.
 *
 * ui_idle_ms is milliseconds since the last raw touch, supplied by the
 * caller rather than read here: svc cannot reach the UI or the touch
 * driver, and passing it in is also what keeps this function a pure
 * function of its inputs for the host test.
 *
 * Effective idle is the MINIMUM of the two sources -- the panel is idle
 * only when BOTH the user and the music have gone quiet. */
void svc_power_tick(svc_power_t *p, uint32_t ui_idle_ms);

/* Force ACTIVE and restart the timer. The wake path: app_wiring calls this
 * when a touch arrives while asleep, since that touch is deliberately
 * swallowed by the UI (it wakes the screen instead of pressing whatever is
 * under the finger) and so would not otherwise register anywhere. Also the
 * natural landing point for a future INTENT_DOMAIN_POWER. */
void svc_power_wake(svc_power_t *p);

static inline bool svc_power_is_asleep(const svc_power_t *p)
{
    return p->mode == (uint8_t)POWER_MODE_ASLEEP;
}

#ifdef __cplusplus
}
#endif

#endif /* SVC_POWER_H */
