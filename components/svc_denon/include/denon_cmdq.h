/*
 * The outbound command queue with coalescing and rate limiting.
 * docs/NETWORKING.md §1.4: dragging a volume dial produces ~60 changes/sec;
 * absolute-value commands (MV today; the slot below is per-class and
 * extensible) coalesce to the newest pending value, rate-limited to one
 * send per 100 ms. Toggle/discrete commands (PW/MU/SI/MS) are never
 * coalesced and never reordered.
 *
 * Interleaving rule (not specified in any doc -- a real arbitration call
 * this file makes): the discrete FIFO always drains first when non-empty;
 * an absolute class only gets a send opportunity once the FIFO is empty.
 * This guarantees a power/mute/input tap never waits behind a volume
 * drag's rate limit.
 *
 * Pure, fixed-capacity, no dynamic allocation, no ESP-IDF, no I/O -- this
 * file only decides WHAT to send next and WHEN; denon_service.c is the one
 * that actually calls denon_transport_if's send_cmd(). Host-tested
 * (test/host/test_denon_cmdq.c) against explicit int64_t timestamps, not
 * an injected clock -- every function here is already parameterized by
 * "now", so there is nothing a DI seam would buy over just passing it.
 */
#ifndef DENON_CMDQ_H
#define DENON_CMDQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest wire command this queue ever stores (matches
 * denon_codec_tables.h's DENON_TABLE_BUF_LEN -- kept as an independent
 * constant rather than including that header, so this module stays
 * protocol-agnostic: it stores opaque strings, it has no idea what MV or
 * MS mean). */
#define DENON_CMDQ_CMD_BUF_LEN 24u

#define DENON_CMDQ_DISCRETE_DEPTH 8u

#define DENON_CMDQ_ABSOLUTE_RATE_LIMIT_US ((int64_t)100 * 1000)

/* One slot today (MV). Add a member here when a second absolute-value
 * command family exists; denon_cmdq_t grows a slot automatically since
 * both arrays below are sized off this enum. */
typedef enum {
    DENON_CMD_CLASS_MV = 0,
    DENON_CMD_CLASS_ABSOLUTE_COUNT,
} denon_cmd_absolute_class_t;

typedef struct {
    char cmd[DENON_CMDQ_CMD_BUF_LEN];
} denon_cmd_t;

typedef struct {
    bool      has_pending[DENON_CMD_CLASS_ABSOLUTE_COUNT];
    denon_cmd_t pending[DENON_CMD_CLASS_ABSOLUTE_COUNT];
    bool        ever_sent[DENON_CMD_CLASS_ABSOLUTE_COUNT];
    int64_t     last_sent_us[DENON_CMD_CLASS_ABSOLUTE_COUNT];   /* valid only if ever_sent[cls] */

    denon_cmd_t discrete[DENON_CMDQ_DISCRETE_DEPTH];
    size_t        discrete_head;
    size_t        discrete_count;
} denon_cmdq_t;

void denon_cmdq_init(denon_cmdq_t *q);

/* Replaces any not-yet-sent pending value for this class -- the coalesce.
 * cmd is copied (truncation is a caller bug, not handled here: cmd must
 * already fit DENON_CMDQ_CMD_BUF_LEN, same contract as denon_*_format()'s
 * own buffer sizing upstream). */
void denon_cmdq_submit_absolute(denon_cmdq_t *q, denon_cmd_absolute_class_t cls, const char *cmd);

/* FIFO append. Returns false (does not insert, does not evict) if the FIFO
 * is already full -- dropping the newest arrival preserves both "never
 * coalesced" and "never reordered" for whatever is already queued. */
bool denon_cmdq_submit_discrete(denon_cmdq_t *q, const char *cmd);

/* Returns the next command to actually send onto the wire, or false if
 * nothing is due right now (an empty FIFO and every pending absolute
 * command still inside its 100ms rate-limit window). Discrete FIFO drains
 * first; at most one absolute-class send per call, chosen from whichever
 * class is both pending and past its rate limit (lowest class index wins
 * ties -- today there is only one class, so this never matters yet). */
bool denon_cmdq_pop_due(denon_cmdq_t *q, int64_t now_us, denon_cmd_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DENON_CMDQ_H */
