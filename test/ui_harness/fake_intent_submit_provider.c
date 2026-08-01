#include "fake_intent_submit_provider.h"

#include <stdio.h>

static bool fake_submit(void *ctx, intent_t *intent)
{
    fake_intent_submit_provider_t *fake = (fake_intent_submit_provider_t *)ctx;
    intent->seq = ++fake->next_seq;

    printf("ui_harness: intent submitted -- domain=%d kind=%u seq=%u\n",
           (int)intent->domain, (unsigned)intent->kind, (unsigned)intent->seq);

    event_t evt = { .topic = TOPIC_INTENT_ACK };
    evt.u.intent_ack.seq  = intent->seq;
    evt.u.intent_ack.kind = INTENT_ACK_EVT_ACCEPTED;
    core_eventbus_publish(fake->bus, &evt);

    return true;
}

void fake_intent_submit_provider_init(fake_intent_submit_provider_t *fake, core_eventbus_t *bus)
{
    fake->bus       = bus;
    fake->next_seq = 0;
}

intent_submit_provider_t fake_intent_submit_provider_get(fake_intent_submit_provider_t *fake)
{
    intent_submit_provider_t p = { .ctx = fake, .submit = fake_submit };
    return p;
}
