/*
 * Intent -> outgoing wire command translation. See denon_intent.h.
 * No allocation, no I/O, no ESP-IDF. Host-tested
 * (test/host/test_denon_intent.c), including the fuzzed-intent-stream
 * exit criterion (docs/ROADMAP.md Phase 3).
 */
#include "denon_intent.h"

#include <string.h>

#include "denon_codec.h"
#include "denon_codec_tables.h"

denon_intent_result_t denon_intent_to_cmd(const intent_t *intent, uint16_t volume_limit_half_db,
                                             char *out_cmd, size_t out_cap,
                                             bool *out_is_absolute, denon_cmd_absolute_class_t *out_class)
{
    if (intent == NULL || out_cmd == NULL || out_is_absolute == NULL || out_class == NULL) {
        return DENON_INTENT_RESULT_UNKNOWN_KIND;
    }
    if (intent->domain != INTENT_DOMAIN_DENON) {
        return DENON_INTENT_RESULT_WRONG_DOMAIN;
    }

    denon_intent_payload_t payload;
    memcpy(&payload, intent->payload, sizeof(payload));

    switch ((denon_intent_kind_t)intent->kind) {
    case DENON_INTENT_SET_POWER: {
        denon_table_status_t st = denon_pw_format(payload.power_on ? DENON_PW_ON : DENON_PW_STANDBY,
                                                     out_cmd, out_cap);
        if (st != DENON_TABLE_OK) return DENON_INTENT_RESULT_UNKNOWN_KIND;
        *out_is_absolute = false;
        return DENON_INTENT_RESULT_OK;
    }
    case DENON_INTENT_SET_VOLUME: {
        /* docs/RISKS.md R13 / docs/adr/0007: the clamp is applied HERE,
         * at the protocol boundary -- this is the one call site. */
        uint16_t clamped = denon_clamp_volume(payload.volume_half_db, volume_limit_half_db);
        denon_codec_status_t st = denon_mv_format(clamped, out_cmd, out_cap);
        if (st != DENON_CODEC_OK) return DENON_INTENT_RESULT_UNKNOWN_KIND;
        *out_is_absolute = true;
        *out_class        = DENON_CMD_CLASS_MV;
        return DENON_INTENT_RESULT_OK;
    }
    case DENON_INTENT_SET_MUTE: {
        denon_table_status_t st = denon_mu_format(payload.muted ? DENON_MU_ON : DENON_MU_OFF,
                                                     out_cmd, out_cap);
        if (st != DENON_TABLE_OK) return DENON_INTENT_RESULT_UNKNOWN_KIND;
        *out_is_absolute = false;
        return DENON_INTENT_RESULT_OK;
    }
    case DENON_INTENT_SET_INPUT: {
        denon_table_status_t st = denon_si_format((denon_input_id_t)payload.input_id, out_cmd, out_cap);
        if (st != DENON_TABLE_OK) return DENON_INTENT_RESULT_UNKNOWN_KIND;
        *out_is_absolute = false;
        return DENON_INTENT_RESULT_OK;
    }
    case DENON_INTENT_SET_LISTENING_MODE: {
        denon_table_status_t st = denon_ms_format((denon_listening_mode_id_t)payload.listening_mode_id,
                                                     out_cmd, out_cap);
        if (st != DENON_TABLE_OK) return DENON_INTENT_RESULT_UNKNOWN_KIND;
        *out_is_absolute = false;
        return DENON_INTENT_RESULT_OK;
    }
    default:
        return DENON_INTENT_RESULT_UNKNOWN_KIND;
    }
}
