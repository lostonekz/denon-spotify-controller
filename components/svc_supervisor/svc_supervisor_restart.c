#include "svc_supervisor_restart.h"

#include <string.h>

void restart_history_init(restart_history_t *h)
{
    memset(h, 0, sizeof(*h));
}

static void compact_expired(restart_history_t *h, int64_t now_us, int64_t window_us)
{
    uint8_t write = 0;
    for (uint8_t read = 0; read < h->count; read++) {
        if (now_us - h->attempts[read] <= window_us) {
            h->attempts[write++] = h->attempts[read];
        }
    }
    h->count = write;
}

restart_decision_t restart_history_evaluate(restart_history_t *h, int64_t now_us,
                                               int64_t window_us, uint8_t max_attempts,
                                               bool degraded_ok)
{
    compact_expired(h, now_us, window_us);

    if (h->count >= max_attempts) {
        return degraded_ok ? RESTART_DECISION_DEGRADE : RESTART_DECISION_FAIL;
    }

    if (h->count >= RESTART_HISTORY_CAPACITY) {
        /* Shouldn't happen given max_attempts is expected to stay within
         * RESTART_HISTORY_CAPACITY, but stay correct (drop the oldest)
         * rather than overflow the array. */
        memmove(&h->attempts[0], &h->attempts[1], (size_t)(h->count - 1) * sizeof(h->attempts[0]));
        h->count--;
    }
    h->attempts[h->count++] = now_us;

    return RESTART_DECISION_ATTEMPT;
}
