/*
 * Phase 0 bandwidth spike — the measurement that decides whether the
 * architecture in docs/ survives contact with the hardware.
 *
 * docs/RISKS.md R1: the ST7701 is on the parallel RGB bus, so panel DMA reads
 * the framebuffer from PSRAM continuously (~27 MB/s at 480x480x16bpp x60Hz).
 * WiFi DMA, TLS working memory and JPEG decode contend for the same bus. The
 * symptoms -- tearing, glitch lines, a drifting image -- appear ONLY under
 * combined load, which means they appear late, in front of the user, and never
 * on the bench.
 *
 * This spike creates that combined load deliberately, on purpose, in week one.
 *
 * THIS IS THROWAWAY CODE. It is not architecture, it does not follow the
 * layering rules, and it will be deleted after Phase 0. Its only job is to
 * produce numbers that are either reassuring or terrifying, before anything
 * is built on top of the assumptions.
 *
 * Exit criteria (docs/ROADMAP.md Phase 0): a written decision on framebuffer
 * count, refresh rate, and bounce-buffer size, backed by these numbers.
 */
#ifndef SPIKE_PHASE0_H
#define SPIKE_PHASE0_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* --- display ---
     * frames_rendered and vsyncs_total are two INDEPENDENT counters (CPU
     * render-loop iterations vs. fixed-rate panel VSYNC ticks) and are not
     * the same denominator. drop rate is frames_dropped/vsyncs_total, not
     * frames_dropped/frames_rendered -- dividing by the wrong one produced a
     * >100% "drop rate" the first time this spike ran on real hardware. */
    uint32_t frames_rendered;
    uint32_t vsyncs_total;        /* panel VSYNC ticks during the run */
    uint32_t frames_dropped;      /* VSYNC arrived with the buffer unready */
    float    fps_avg;
    float    fps_min;             /* worst 1-second window: the number that matters */
    uint32_t longest_frame_us;

    /* --- memory, sampled at peak load --- */
    uint32_t internal_free_min;   /* bytes, MALLOC_CAP_INTERNAL */
    uint32_t psram_free_min;      /* bytes, MALLOC_CAP_SPIRAM   */
    uint32_t internal_largest_block;

    /* --- TLS --- */
    uint32_t tls_heap_peak;       /* internal SRAM consumed by one session */
    uint32_t tls_handshake_ms;
    uint32_t bytes_downloaded;
    float    download_kbps;

    /* --- JPEG --- */
    uint32_t jpeg_decode_ms_avg;
    uint32_t jpeg_decode_ms_max;
    uint32_t jpeg_decodes;
    uint32_t frames_dropped_during_jpeg;

    /* --- NVS --- */
    uint32_t nvs_write_ms_avg;
    uint32_t nvs_write_ms_max;    /* a long flash write blocks the bus */
    uint32_t nvs_writes;
    uint32_t frames_dropped_during_nvs;

    /* --- environment --- */
    float    temp_c_start;
    float    temp_c_end;
    uint32_t duration_s;
} spike_report_t;

typedef struct {
    uint32_t duration_s;          /* how long to hold combined load */
    bool     enable_wifi_load;
    bool     enable_tls_load;
    bool     enable_jpeg_load;
    bool     enable_nvs_load;
    const char *download_url;     /* HTTPS, a few MB, for the TLS+WiFi load */
    const char *wifi_ssid;
    const char *wifi_password;
} spike_config_t;

/* Runs the spike to completion and fills `out`. Blocking; this is the whole
 * program during Phase 0. */
void spike_phase0_run(const spike_config_t *cfg, spike_report_t *out);

/* Prints the report in a form that can be pasted straight into
 * docs/MEMORY.md §1 and docs/RISKS.md R1. */
void spike_phase0_print(const spike_report_t *r);

#ifdef __cplusplus
}
#endif

#endif /* SPIKE_PHASE0_H */
