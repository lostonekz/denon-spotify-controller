#include "svc_logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#define LOG_LINE_MAX 256

/* esp_log_set_vprintf's hook signature has no ctx parameter, so the active
 * logger has to live in a file-scope singleton -- there is only ever one
 * esp_log system per process anyway. */
static svc_logging_t *g_logger = NULL;

/* Overwrite-oldest is this module's policy, not core_pool_ring's (that
 * header is explicit that "what happens when the ring is full... lives one
 * layer up"), so it stays expressed here -- but composed from the ring's
 * bulk primitives instead of a per-byte loop.
 *
 * This runs inside the esp_log vprintf hook, i.e. on whichever task called
 * ESP_LOGx, with the logging mutex held. The old loop did one
 * core_pool_ring_push() call per byte -- roughly 256 calls, each with its
 * own full/bounds check, modulo and single-byte memcpy, for one log line.
 * This does at most three memcpy-class operations total regardless of line
 * length, which matters because every log line in the system pays it.
 *
 * The `len >= cap` case is not just an optimisation: without it, a line
 * longer than the whole ring would discard and re-push its way through the
 * buffer repeatedly, ending with only its tail retained anyway. Trimming
 * up front makes that outcome explicit and bounds the work. */
static void ring_write_overwriting(core_pool_ring_t *ring, const char *data, size_t len)
{
    size_t cap = core_pool_ring_capacity(ring);
    if (cap == 0 || len == 0) {
        return;
    }
    if (len >= cap) {
        /* Only the final `cap` bytes could survive -- keep the newest,
         * matching the overwrite-oldest policy this function implements. */
        data += (len - cap);
        len   = cap;
    }

    size_t free_slots = cap - core_pool_ring_count(ring);
    if (len > free_slots) {
        core_pool_ring_discard(ring, len - free_slots);
    }
    core_pool_ring_push_bulk(ring, data, len);
}

static int log_vprintf_hook(const char *fmt, va_list args)
{
    svc_logging_t *s = g_logger;
    if (s == NULL) {
        return vprintf(fmt, args);
    }

    char buf[LOG_LINE_MAX];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n < 0) {
        return n;
    }
    size_t written = ((size_t)n < sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;

    svc_logging_redact_apply(buf, written, s->rules, s->rules_count);

    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    ring_write_overwriting(&s->ring, buf, written);
    xSemaphoreGive(s->lock_sem);

    /* Print the redacted buffer, not the original fmt/args -- a secret that
     * was just scrubbed for the ring must not reach the console either. */
    fwrite(buf, 1, written, stdout);
    return (int)written;
}

bool svc_logging_create(svc_logging_t *s, const redact_rule_t *rules, size_t rules_count)
{
    memset(s, 0, sizeof(*s));
    core_pool_ring_init(&s->ring, s->ring_storage, 1, SVC_LOGGING_RING_BYTES);
    s->rules       = rules;
    s->rules_count = rules_count;

    s->lock_sem = xSemaphoreCreateMutex();
    if (s->lock_sem == NULL) {
        return false;
    }

    g_logger = s;
    esp_log_set_vprintf(log_vprintf_hook);
    return true;
}

size_t svc_logging_dump(svc_logging_t *s, char *out, size_t out_capacity)
{
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);

    size_t count = core_pool_ring_count(&s->ring);
    size_t n = (count < out_capacity) ? count : out_capacity;

    for (size_t i = 0; i < n; i++) {
        size_t idx = (s->ring.tail + i) % s->ring.capacity;
        out[i] = (char)s->ring.storage[idx];
    }

    xSemaphoreGive(s->lock_sem);
    return n;
}
