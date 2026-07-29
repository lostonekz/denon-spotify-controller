/*
 * Phase 0 load-generating tasks. THROWAWAY. See spike_phase0.h.
 *
 * Split out of spike_runner.c purely to stay under tools/check_limits.py's
 * 400-line limit -- spike_runner.c keeps orchestration (spike_phase0_run)
 * and the render loop; this file holds everything that pushes bus traffic
 * (JPEG, NVS, network, and the heap watermark sampler) so spike_phase0_run
 * has something to point at when it creates each task.
 */
#ifndef SPIKE_LOAD_TASKS_H
#define SPIKE_LOAD_TASKS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "spike_display_load.h"
#include "spike_phase0.h"

typedef struct {
    volatile bool     running;
    volatile uint32_t jpeg_decodes;
    volatile uint32_t jpeg_ms_total;
    volatile uint32_t jpeg_ms_max;
    volatile uint32_t nvs_writes;
    volatile uint32_t nvs_ms_total;
    volatile uint32_t nvs_ms_max;
    volatile uint32_t drops_at_nvs_start;
    volatile uint32_t drops_during_nvs;
    volatile uint32_t drops_during_jpeg;
    volatile uint32_t internal_min;
    volatile uint32_t psram_min;
    volatile uint32_t tls_heap_peak;
    volatile uint32_t tls_handshake_ms;
    volatile uint32_t bytes_downloaded;
    volatile float    download_kbps;
    volatile uint32_t net_downloads_attempted;
    volatile uint32_t net_downloads_ok;
    volatile uint32_t net_active_ms;
    volatile uint32_t drops_during_net;
    spike_display_t  *disp;
    const spike_config_t *cfg;
} spike_ctx_t;

esp_err_t init_nvs_for_spike(void);

void jpeg_load_task(void *arg);
void nvs_load_task(void *arg);
void net_load_task(void *arg);
void watermark_task(void *arg);

#endif /* SPIKE_LOAD_TASKS_H */
