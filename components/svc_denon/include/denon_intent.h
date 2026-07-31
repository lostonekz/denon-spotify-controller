/*
 * Translates a Denon intent into an outgoing wire command. Pure: no
 * ESP-IDF, no I/O, no locking -- the piece docs/ROADMAP.md Phase 3's exit
 * criterion ("the safety clamp holds under a fuzzed intent stream") tests
 * directly, independent of svc_denon_t's real cmdq/mutex/task machinery.
 *
 * docs/RISKS.md R13 / docs/adr/0007: the volume clamp is applied HERE, at
 * the point an outgoing MV command is formed -- the protocol boundary --
 * not upstream. There is exactly one call site to audit.
 */
#ifndef DENON_INTENT_H
#define DENON_INTENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "denon_cmdq.h"
#include "intent_types.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    uint16_t volume_half_db;     /* DENON_INTENT_SET_VOLUME -- pre-clamp; this file clamps */
    bool muted;                  /* DENON_INTENT_SET_MUTE */
    uint8_t input_id;            /* DENON_INTENT_SET_INPUT -- denon_input_id_t */
    uint8_t listening_mode_id;   /* DENON_INTENT_SET_LISTENING_MODE -- denon_listening_mode_id_t */
} denon_intent_payload_t;

typedef enum {
    DENON_INTENT_RESULT_OK = 0,
    DENON_INTENT_RESULT_WRONG_DOMAIN,   /* intent->domain != INTENT_DOMAIN_DENON */
    DENON_INTENT_RESULT_UNKNOWN_KIND,   /* domain matched but kind isn't a real denon_intent_kind_t */
} denon_intent_result_t;

/* out_cmd must be at least DENON_CMDQ_CMD_BUF_LEN bytes. *out_is_absolute
 * tells the caller which denon_cmdq_t submit function to use
 * (denon_cmdq_submit_absolute with *out_class, or submit_discrete). */
denon_intent_result_t denon_intent_to_cmd(const intent_t *intent, uint16_t volume_limit_half_db,
                                             char *out_cmd, size_t out_cap,
                                             bool *out_is_absolute, denon_cmd_absolute_class_t *out_class);

#ifdef __cplusplus
}
#endif

#endif /* DENON_INTENT_H */
