/* Phase 0 network load. THROWAWAY. Private to spike_phase0. */
#ifndef SPIKE_NET_LOAD_H
#define SPIKE_NET_LOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t bytes;
    uint32_t handshake_ms;
    uint32_t tls_heap_peak;   /* internal SRAM consumed by the session */
    float    kbps;
    bool     ok;
} spike_net_result_t;

esp_err_t spike_net_connect(const char *ssid, const char *password);

/* Blocking HTTPS GET, discarding the body. Samples internal-SRAM free before,
 * during and after so the TLS session cost is attributable. */
void spike_net_download(const char *url, spike_net_result_t *out);

#endif /* SPIKE_NET_LOAD_H */
