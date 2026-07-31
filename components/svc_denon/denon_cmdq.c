/*
 * Coalescing/rate-limited outbound command queue. See denon_cmdq.h.
 * No allocation, no I/O, no ESP-IDF. Host-tested.
 */

#include "denon_cmdq.h"

#include <string.h>

static void copy_cmd(denon_cmd_t *dst, const char *cmd)
{
    strncpy(dst->cmd, cmd, DENON_CMDQ_CMD_BUF_LEN - 1);
    dst->cmd[DENON_CMDQ_CMD_BUF_LEN - 1] = '\0';
}

void denon_cmdq_init(denon_cmdq_t *q)
{
    memset(q, 0, sizeof(*q));
    /* ever_sent[] is already false from the memset -- first pending value
     * per class goes out immediately, not after an initial rate-limit wait. */
}

void denon_cmdq_submit_absolute(denon_cmdq_t *q, denon_cmd_absolute_class_t cls, const char *cmd)
{
    if (cls >= DENON_CMD_CLASS_ABSOLUTE_COUNT || cmd == NULL) {
        return;
    }
    copy_cmd(&q->pending[cls], cmd);
    q->has_pending[cls] = true;
}

bool denon_cmdq_submit_discrete(denon_cmdq_t *q, const char *cmd)
{
    if (cmd == NULL || q->discrete_count >= DENON_CMDQ_DISCRETE_DEPTH) {
        return false;
    }
    size_t tail = (q->discrete_head + q->discrete_count) % DENON_CMDQ_DISCRETE_DEPTH;
    copy_cmd(&q->discrete[tail], cmd);
    q->discrete_count++;
    return true;
}

static bool pop_discrete(denon_cmdq_t *q, denon_cmd_t *out)
{
    if (q->discrete_count == 0) {
        return false;
    }
    *out = q->discrete[q->discrete_head];
    q->discrete_head = (q->discrete_head + 1) % DENON_CMDQ_DISCRETE_DEPTH;
    q->discrete_count--;
    return true;
}

static bool pop_absolute_due(denon_cmdq_t *q, int64_t now_us, denon_cmd_t *out)
{
    for (size_t cls = 0; cls < DENON_CMD_CLASS_ABSOLUTE_COUNT; cls++) {
        if (!q->has_pending[cls]) {
            continue;
        }
        if (q->ever_sent[cls] && (now_us - q->last_sent_us[cls] < DENON_CMDQ_ABSOLUTE_RATE_LIMIT_US)) {
            continue;   /* still inside this class's rate-limit window */
        }
        *out = q->pending[cls];
        q->has_pending[cls]  = false;
        q->ever_sent[cls]    = true;
        q->last_sent_us[cls] = now_us;
        return true;
    }
    return false;
}

bool denon_cmdq_pop_due(denon_cmdq_t *q, int64_t now_us, denon_cmd_t *out)
{
    if (out == NULL) {
        return false;
    }
    if (pop_discrete(q, out)) {
        return true;
    }
    return pop_absolute_due(q, now_us, out);
}
