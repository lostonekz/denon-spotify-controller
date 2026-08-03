/*
 * intent_t.kind/payload when intent_t.domain == INTENT_DOMAIN_SETTINGS.
 *
 * INTENT_DOMAIN_SETTINGS has existed in intent_types.h since Phase 1 but had
 * no payload shape and no dispatch case anywhere -- the Connection screen's
 * volume-cap slider (w_volume_cap_slider) is the first real submitter.
 * Lives in core_types, same "ui can construct it, svc_settings stays svc-
 * layer-only" reasoning already applied to denon_intent_types.h/
 * playback_intent_types.h: `ui` may not include svc_settings.h
 * (tools/arch_rules.json), so the intent's own shape has to live somewhere
 * both `ui` and `main` (which does the actual settings_set_u8() call) can
 * reach without a forbidden layer edge.
 */
#ifndef CORE_SETTINGS_INTENT_TYPES_H
#define CORE_SETTINGS_INTENT_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTINGS_INTENT_SET_VOLUME_LIMIT = 0,
} settings_intent_kind_t;

/* Packed into intent_t.payload (16 B, plenty of room). Half-dB, same
 * representation as denon_intent_payload_t.volume_half_db and
 * SETTING_VOLUME_LIMIT_MAX's own stored u8 -- main/app_wiring_ticks.c
 * narrows to uint8_t at the settings_set_u8() call site, not here. */
typedef struct {
    uint16_t volume_limit_half_db;   /* SETTINGS_INTENT_SET_VOLUME_LIMIT */
} settings_intent_payload_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_SETTINGS_INTENT_TYPES_H */
