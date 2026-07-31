/*
 * HTTP retry decision -- the pure policy half of net_http, split from the
 * esp_http_client transport exactly like ota_policy.c is split from
 * svc_ota's transport. docs/NETWORKING.md §1.6:
 *
 *   typedef struct {
 *       uint16_t connect_timeout_ms;   5000
 *       uint16_t request_timeout_ms;   8000
 *       uint8_t  max_retries;          2
 *       uint16_t backoff_base_ms;      500
 *       bool     retry_on_5xx;         true
 *       bool     honour_retry_after;   true
 *   } http_policy_t;
 *
 * "Spotify, OTA, and artwork each get different policies. Timeouts are
 * always set." Different consumers pass different http_policy_t values;
 * this module owns none of the values itself, only the decision.
 */
#ifndef NET_HTTP_RETRY_POLICY_H
#define NET_HTTP_RETRY_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t connect_timeout_ms;
    uint16_t request_timeout_ms;
    uint8_t  max_retries;
    uint16_t backoff_base_ms;
    bool     retry_on_5xx;
    bool     honour_retry_after;
} http_policy_t;

typedef enum {
    HTTP_RETRY_NO = 0,   /* give up; return the last result to the caller */
    HTTP_RETRY_YES,      /* retry after *out_delay_ms */
} http_retry_decision_t;

/* `attempt` is 0-based and counts attempts already made (0 = the first
 * attempt just completed). A response is retryable if it timed out, or if
 * it's a 429, or if it's a 5xx and policy->retry_on_5xx. Once retries are
 * exhausted (attempt >= policy->max_retries), always HTTP_RETRY_NO.
 *
 * Delay: a 429 with policy->honour_retry_after and a nonzero
 * `retry_after_s` uses that value exactly, overriding the normal backoff
 * (the server said how long to wait; that's more authoritative than our
 * own guess). Every other retryable case uses exponential backoff from
 * policy->backoff_base_ms, doubling per attempt. */
http_retry_decision_t net_http_retry_decide(const http_policy_t *policy, uint8_t attempt,
                                               int status_code, bool timed_out,
                                               uint32_t retry_after_s, uint32_t *out_delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* NET_HTTP_RETRY_POLICY_H */
