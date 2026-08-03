/*
 * intent_t.kind/payload when intent_t.domain == INTENT_DOMAIN_DENON, plus
 * denon_input_id_t (the stable input-select ordinal denon_intent_payload_t.
 * input_id stores).
 *
 * Lives in core_types (layer "core"), not in svc_denon's own denon_intent.h/
 * denon_codec_tables.h where they were originally defined -- Art-forward
 * redesign: the new w_power_glyph widget needs to construct a real
 * DENON_INTENT_SET_POWER + DENON_INTENT_SET_INPUT pair on tap (turn the
 * receiver on and switch to the HEOS Music input, or switch to TV Audio and
 * power off), but `ui` may not include anything from `svc_denon`
 * (tools/arch_rules.json: ui -> [core, lvgl, idf, ui_hal_glue]). Moved, not
 * duplicated -- same "one canonical definition, no drift risk" reasoning
 * already applied to playback_intent_types.h/multi_intent_types.h for the
 * identical svc_spotify/w_transport and app_wiring/w_volume_slider problem.
 * denon_intent.h and denon_codec_tables.h each include this file as their
 * own canonical definition of the pieces they used to own.
 *
 * denon_listening_mode_id_t / the PW/MU wire-codec enums (denon_power_t/
 * denon_mute_t) stay in denon_codec_tables.h -- nothing in `ui` needs them,
 * so moving them would be scope creep beyond what this widget requires.
 *
 * DENON_MV_HALF_DB_MIN/MAX (below) moved from denon_codec.h for the same
 * reason: w_volume_slider needs the real clamp domain for lv_slider_set_
 * range(), not a hardcoded "197" duplicate that could silently drift from
 * the codec's own definition.
 */
#ifndef CORE_DENON_INTENT_TYPES_H
#define CORE_DENON_INTENT_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The stable ordinal denon_state_t.input_id / denon_intent_payload_t.input_id
 * store -- never the wire string; denon_codec_tables.c owns the ordinal<->
 * wire-code mapping (denon_si_parse()/denon_si_format()), unchanged by this
 * move. */
/* Denon absolute volume scale is 0.0 .. 98.0 dB in 0.5 steps (98.5 exists on
 * the wire on some models) -- moved from denon_codec.h, canonical there via
 * this header's own include. */
#define DENON_MV_HALF_DB_MIN   0u
#define DENON_MV_HALF_DB_MAX   197u   /* 98.5 dB */

typedef enum {
    DENON_INPUT_PHONO = 0,
    DENON_INPUT_CD,
    DENON_INPUT_TUNER,
    DENON_INPUT_DVD,
    DENON_INPUT_BD,
    DENON_INPUT_TV,
    DENON_INPUT_SAT_CBL,
    DENON_INPUT_MPLAY,
    DENON_INPUT_GAME,
    DENON_INPUT_AUX1,
    DENON_INPUT_NET,        /* Denon AVR-X-series UIs label this input "HEOS Music" */
    DENON_INPUT_BT,
    DENON_INPUT_COUNT,      /* not a real input -- array-sizing sentinel */
} denon_input_id_t;

/* intent_t.kind when intent_t.domain == INTENT_DOMAIN_DENON. SET_VOLUME's
 * payload is an absolute half-dB target, not a delta -- matches
 * denon_codec.h's own representation and adr/0007's "the real analog
 * attenuator" framing. */
typedef enum {
    DENON_INTENT_SET_POWER = 0,
    DENON_INTENT_SET_VOLUME,
    DENON_INTENT_SET_MUTE,
    DENON_INTENT_SET_INPUT,
    DENON_INTENT_SET_LISTENING_MODE,
} denon_intent_kind_t;

/* Packed into intent_t.payload (16 B, plenty of room). */
typedef struct {
    bool power_on;               /* DENON_INTENT_SET_POWER */
    uint16_t volume_half_db;     /* DENON_INTENT_SET_VOLUME -- pre-clamp; denon_intent.c clamps */
    bool muted;                  /* DENON_INTENT_SET_MUTE */
    uint8_t input_id;            /* DENON_INTENT_SET_INPUT -- denon_input_id_t */
    uint8_t listening_mode_id;   /* DENON_INTENT_SET_LISTENING_MODE -- denon_listening_mode_id_t */
} denon_intent_payload_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_DENON_INTENT_TYPES_H */
