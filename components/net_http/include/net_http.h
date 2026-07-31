/*
 * HTTP(S) client, target-only glue around net_http_retry_policy.c's pure
 * decision function. docs/MEMORY.md §4's TLS memory arbiter: a counting
 * semaphore caps concurrent TLS sessions at 2 (mbedTLS ~40 KB/session; 3
 * concurrent would exhaust internal SRAM). "OTA takes both slots" is
 * expressed here as whoever's downloading an image calling
 * net_http_tls_acquire(h, 2, ...) around its own stream_open/read/close
 * loop, rather than net_http enforcing that policy itself -- net_http
 * knows nothing about OTA, artwork, or Spotify; it only knows slots.
 *
 * Two entry points:
 *   - net_http_get(): buffered, for small responses (an OTA manifest).
 *     Applies the retry policy internally, acquiring/releasing 1 TLS slot
 *     per attempt.
 *   - net_http_stream_open/read/close(): raw streaming, for a large body
 *     (an OTA image) that cannot fit in one buffer. Deliberately NOT
 *     retried internally -- what "retry" means for a partially-streamed
 *     multi-megabyte download (restart from scratch vs. resume) is a
 *     decision for the caller that actually knows the content (svc_ota),
 *     not something net_http can decide generically. The caller manages
 *     the TLS arbiter slot(s) itself around open()/close().
 */
#ifndef NET_HTTP_H
#define NET_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "lifecycle_types.h"
#include "net_http_retry_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    SemaphoreHandle_t tls_slots;   /* counting semaphore, capacity 2 */
} net_http_t;

bool net_http_create(net_http_t *h);

/* Blocks up to timeout_ms waiting for n_slots to become free. On failure,
 * releases whatever it already grabbed this call before returning false --
 * never leaks a partial acquisition. */
bool net_http_tls_acquire(net_http_t *h, uint8_t n_slots, uint32_t timeout_ms);
void net_http_tls_release(net_http_t *h, uint8_t n_slots);

typedef struct {
    int    status_code;
    size_t   len;   /* bytes actually received into out_buf */
} net_http_response_t;

/* `resp->len` may be less than the real response body length if out_cap
 * was too small (truncated, not an error). Returns false only if every
 * attempt failed to even get a response (network/TLS failure each time);
 * a non-2xx status the caller must judge for itself is still `true`. */
bool net_http_get(net_http_t *h, const char *url, const http_policy_t *policy,
                    char *out_buf, size_t out_cap, net_http_response_t *resp);

typedef struct {
    void *client;   /* esp_http_client_handle_t, opaque -- keeps esp_http_client.h
                       out of every consumer's includes (e.g. svc_ota) */
} net_http_stream_t;

/* Opens a GET request and reads the response headers. `policy`'s
 * `connect_timeout_ms`/`request_timeout_ms` collapse onto
 * esp_http_client's single overall `timeout_ms` -- it has no separate
 * connect-phase timeout to set. `*out_retry_after_s` is the numeric
 * Retry-After header value if present and parseable as seconds (0
 * otherwise; an HTTP-date form is not parsed, a documented simplification). */
bool net_http_stream_open(const char *url, const http_policy_t *policy,
                             net_http_stream_t *stream, int *out_status_code,
                             uint32_t *out_retry_after_s);

/* 0 = EOF, negative = error. */
int net_http_stream_read(net_http_stream_t *stream, void *buf, size_t buf_len);

void net_http_stream_close(net_http_stream_t *stream);

/* Not in LIFECYCLE.md §1.2's restart table -- defaults to the conservative
 * choice, same reasoning as net_wifi/net_time/svc_ota. degraded_ok = true:
 * no HTTP just means no OTA/Spotify/artwork fetches succeed, not fatal. */
static const service_desc_t SVC_NET_HTTP_SERVICE_DESC = {
    .id           = SVC_ID_NET_HTTP,
    .name         = "net_http",
    .requires     = 0,
    .restart      = RESTART_REBOOT,
    .max_restarts = 0,
    .degraded_ok  = true,
    .teardown     = NULL,
    .restart_fn   = NULL,
    .restart_ctx  = NULL,
};

#ifdef __cplusplus
}
#endif

#endif /* NET_HTTP_H */
