#include "core_state.h"

#include <stdint.h>
#include <string.h>

static void lock(core_state_t *s)   { s->lock.lock(s->lock.ctx); }
static void unlock(core_state_t *s) { s->lock.unlock(s->lock.ctx); }

typedef struct {
    void   *ptr;
    size_t  size;
} domain_slot_t;

static domain_slot_t domain_slot(core_state_t *s, domain_id_t domain)
{
    switch (domain) {
    case DOMAIN_NET:      return (domain_slot_t){ &s->state.net,      sizeof(s->state.net) };
    case DOMAIN_SPOTIFY:  return (domain_slot_t){ &s->state.spotify,  sizeof(s->state.spotify) };
    case DOMAIN_PLAYBACK: return (domain_slot_t){ &s->state.playback, sizeof(s->state.playback) };
    case DOMAIN_ARTWORK:  return (domain_slot_t){ &s->state.artwork,  sizeof(s->state.artwork) };
    case DOMAIN_DENON:    return (domain_slot_t){ &s->state.denon,    sizeof(s->state.denon) };
    case DOMAIN_POWER:    return (domain_slot_t){ &s->state.power,    sizeof(s->state.power) };
    case DOMAIN_OTA:      return (domain_slot_t){ &s->state.ota,      sizeof(s->state.ota) };
    case DOMAIN_UI:       return (domain_slot_t){ &s->state.ui,       sizeof(s->state.ui) };
    case DOMAIN_COUNT:
    default:              return (domain_slot_t){ NULL, 0 };
    }
}

void core_state_init(core_state_t *s, os_lock_if_t lock_if, time_source_t clock)
{
    memset(&s->state, 0, sizeof(s->state));
    s->lock  = lock_if;
    s->clock = clock;
}

bool core_state_read(core_state_t *s, domain_id_t domain, void *out, size_t out_size)
{
    domain_slot_t slot = domain_slot(s, domain);
    if (slot.ptr == NULL || slot.size != out_size) {
        return false;
    }

    lock(s);
    memcpy(out, slot.ptr, slot.size);
    unlock(s);
    return true;
}

bool core_state_write(core_state_t *s, domain_id_t domain, link_status_t status,
                       const void *in, size_t in_size)
{
    domain_slot_t slot = domain_slot(s, domain);
    if (slot.ptr == NULL || slot.size != in_size) {
        return false;
    }

    lock(s);

    /* Captured before the memcpy below: `in` carries its own (usually
     * zeroed) meta field, which would otherwise clobber the running version
     * counter the instant it's copied over slot.ptr. */
    domain_meta_t *meta = (domain_meta_t *)slot.ptr;   /* meta is every domain struct's first member */
    uint32_t next_version = meta->version + 1;

    memcpy(slot.ptr, in, slot.size);

    meta->status     = status;
    meta->updated_us = s->clock.now_us(s->clock.ctx);
    meta->version     = next_version;

    unlock(s);
    return true;
}
