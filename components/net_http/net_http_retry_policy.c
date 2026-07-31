#include "net_http_retry_policy.h"

#define BACKOFF_MAX_EXPONENT 10   /* guards the left-shift; not a documented cap, just overflow safety */

http_retry_decision_t net_http_retry_decide(const http_policy_t *policy, uint8_t attempt,
                                               int status_code, bool timed_out,
                                               uint32_t retry_after_s, uint32_t *out_delay_ms)
{
    if (attempt >= policy->max_retries) {
        return HTTP_RETRY_NO;
    }

    bool is_5xx     = status_code >= 500 && status_code < 600;
    bool is_429     = status_code == 429;
    bool retryable  = timed_out || is_429 || (policy->retry_on_5xx && is_5xx);

    if (!retryable) {
        return HTTP_RETRY_NO;
    }

    if (is_429 && policy->honour_retry_after && retry_after_s > 0) {
        *out_delay_ms = retry_after_s * 1000u;
    } else {
        uint32_t exponent = (attempt < BACKOFF_MAX_EXPONENT) ? attempt : BACKOFF_MAX_EXPONENT;
        *out_delay_ms = (uint32_t)policy->backoff_base_ms << exponent;
    }

    return HTTP_RETRY_YES;
}
