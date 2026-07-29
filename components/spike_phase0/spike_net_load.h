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

/* Diagnostic, run once before the real spike load: ping the gateway and a
 * fixed public IP (basic reachability, no DNS involved), then download a
 * small file from a DIFFERENT host than the main spike target. Added to
 * isolate whether a download failure is specific to the spike's usual
 * target host/Range request or a general problem with this board's
 * WiFi/TLS path -- logs everything, does not affect the spike's own
 * pass/fail verdict. THROWAWAY, remove with the rest of spike_phase0. */
void spike_net_diag(void);

/* Blocking HTTPS GET, discarding the body. Samples internal-SRAM free before,
 * during and after so the TLS session cost is attributable. */
void spike_net_download(const char *url, spike_net_result_t *out);

#endif /* SPIKE_NET_LOAD_H */
