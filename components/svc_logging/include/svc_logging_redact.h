/*
 * Pure prefix-based log scrubbing. docs/SECURITY.md T4: "redaction by key
 * prefix, secrets never rendered" -- rules match a known KEY prefix (e.g.
 * "Authorization: Bearer "), not a specific secret value, since the whole
 * point is to catch secrets we don't already know at build time.
 *
 * No FreeRTOS, no ESP-IDF -- directly host-testable, same as core_fsm.
 * Phase 1 ships with an empty ruleset (no secret-bearing subsystem exists
 * yet); proven against a synthetic secret string in the host test so the
 * mechanism itself is verified ahead of any real consumer.
 */
#ifndef SVC_LOGGING_REDACT_H
#define SVC_LOGGING_REDACT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *prefix;   /* NUL-terminated, matched byte-for-byte */
} redact_rule_t;

/* Scans `text` (text_len bytes, need not be NUL-terminated) for each rule's
 * prefix. At every match, overwrites every byte from the end of the matched
 * prefix up to the next whitespace byte (or the end of the buffer,
 * whichever comes first) with '*'. Operates in place. Returns the number
 * of redactions applied -- 0 if none, including when rules_count == 0 (an
 * empty ruleset is a deliberate, valid configuration, not an error). */
size_t svc_logging_redact_apply(char *text, size_t text_len,
                                  const redact_rule_t *rules, size_t rules_count);

#ifdef __cplusplus
}
#endif

#endif /* SVC_LOGGING_REDACT_H */
