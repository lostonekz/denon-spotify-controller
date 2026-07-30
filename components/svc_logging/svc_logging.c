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

static void ring_write_overwriting(core_pool_ring_t *ring, const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (core_pool_ring_is_full(ring)) {
            uint8_t discard;
            core_pool_ring_pop(ring, &discard);
        }
        uint8_t byte = (uint8_t)data[i];
        core_pool_ring_push(ring, &byte);
    }
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
