/*
 * Reconnect backoff. docs/NETWORKING.md §1.1: "Backoff is exponential with
 * jitter, capped at 60 s. Never a busy retry loop." The doc doesn't pin a
 * base/multiplier/jitter width; this project's choice: full jitter (a
 * uniform random delay in [0, max_delay_for_this_attempt]), base 1000 ms,
 * doubling per attempt, exponent clamped at 6 (so the theoretical max
 * before clamping, base<<6 = 64000 ms, already exceeds the 60 s cap --
 * attempt 6 onward is pinned at the cap).
 *
 * Randomness is injected (net_wifi_rand_fn), same DI seam as
 * time_source_t: real wiring uses esp_random(), host tests use a
 * deterministic fake.
 */
#ifndef NET_WIFI_BACKOFF_H
#define NET_WIFI_BACKOFF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_WIFI_BACKOFF_BASE_MS      1000
#define NET_WIFI_BACKOFF_CAP_MS       60000
#define NET_WIFI_BACKOFF_MAX_EXPONENT 6

typedef uint32_t (*net_wifi_rand_fn)(void *ctx);

typedef struct {
    uint32_t          attempt;    /* consecutive failures so far */
    net_wifi_rand_fn  rand_u32;
    void             *rand_ctx;
} net_wifi_backoff_t;

/* Pure: the upper bound of the jitter range for a given attempt count,
 * with no randomness involved. Exposed on its own so the exponential
 * growth and the cap can be asserted directly, independent of jitter. */
uint32_t net_wifi_backoff_max_delay_ms(uint32_t attempt);

void net_wifi_backoff_init(net_wifi_backoff_t *b, net_wifi_rand_fn rand_u32, void *rand_ctx);

/* Returns a uniform-random delay in [0, net_wifi_backoff_max_delay_ms(attempt)]
 * for the CURRENT attempt count, then increments it for next time. */
uint32_t net_wifi_backoff_next_delay_ms(net_wifi_backoff_t *b);

/* Back to attempt 0 -- call on a successful connection. */
void net_wifi_backoff_reset(net_wifi_backoff_t *b);

#ifdef __cplusplus
}
#endif

#endif /* NET_WIFI_BACKOFF_H */
