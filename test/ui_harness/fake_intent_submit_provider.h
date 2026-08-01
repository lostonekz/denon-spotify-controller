/*
 * Host fixture standing in for the real intent_submit_provider_t
 * (main/app_wiring_ui.c's ui_intent_submit(), wrapping app_wiring_submit_
 * intent()) -- Phase 5 Tier 6. Simulates an immediate-accept backend:
 * submit() stamps a seq and publishes INTENT_ACK_EVT_ACCEPTED on the same
 * bus right away, so the pending -> accepted round trip is visible in the
 * simulator without a real Spotify account. Real reject/timeout behavior
 * needs the real backend -- Tier 7's job, on target.
 */
#ifndef FAKE_INTENT_SUBMIT_PROVIDER_H
#define FAKE_INTENT_SUBMIT_PROVIDER_H

#include <stdint.h>

#include "core_eventbus.h"
#include "intent_submit_types.h"

typedef struct {
    core_eventbus_t *bus;
    uint32_t            next_seq;
} fake_intent_submit_provider_t;

void fake_intent_submit_provider_init(fake_intent_submit_provider_t *fake, core_eventbus_t *bus);
intent_submit_provider_t fake_intent_submit_provider_get(fake_intent_submit_provider_t *fake);

#endif /* FAKE_INTENT_SUBMIT_PROVIDER_H */
