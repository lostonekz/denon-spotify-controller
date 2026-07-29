/*
 * Phase 0 report formatting. Deliberately separate from the measurement code
 * so the output format can be tweaked without touching anything that runs
 * under load.
 *
 * THROWAWAY. See spike_phase0.h.
 */

#include "spike_phase0.h"

#include <stdio.h>

#include "esp_log.h"

static const char *TAG = "spike0";

#define KB(x) ((double)(x) / 1024.0)

void spike_phase0_print(const spike_report_t *r)
{
    if (r == NULL) {
        return;
    }

    printf("\n");
    printf("========================================================\n");
    printf(" PHASE 0 BANDWIDTH SPIKE  (docs/RISKS.md R1)\n");
    printf("========================================================\n");
    printf(" duration                %u s\n", r->duration_s);
    printf("\n-- DISPLAY --------------------------------------------\n");
    printf(" frames rendered         %u\n", r->frames_rendered);
    printf(" frames dropped          %u  (%.2f%%)\n",
           r->frames_dropped,
           r->frames_rendered ? 100.0 * r->frames_dropped / r->frames_rendered : 0.0);
    printf(" fps avg / min           %.1f / %.1f\n", r->fps_avg, r->fps_min);
    printf(" longest frame           %u us\n", r->longest_frame_us);

    printf("\n-- MEMORY (minimum free under load) -------------------\n");
    printf(" internal SRAM free      %.1f KB\n", KB(r->internal_free_min));
    printf(" internal largest block  %.1f KB\n", KB(r->internal_largest_block));
    printf(" PSRAM free              %.1f KB\n", KB(r->psram_free_min));

    printf("\n-- TLS ------------------------------------------------\n");
    printf(" internal heap peak      %.1f KB per session\n", KB(r->tls_heap_peak));
    printf(" handshake               %u ms\n", r->tls_handshake_ms);
    printf(" downloaded              %.1f KB @ %.1f KB/s\n",
           KB(r->bytes_downloaded), r->download_kbps);

    printf("\n-- JPEG DECODE ----------------------------------------\n");
    printf(" decodes                 %u\n", r->jpeg_decodes);
    printf(" avg / max               %u / %u ms\n",
           r->jpeg_decode_ms_avg, r->jpeg_decode_ms_max);

    printf("\n-- NVS ------------------------------------------------\n");
    printf(" writes                  %u\n", r->nvs_writes);
    printf(" avg / max               %u / %u ms\n",
           r->nvs_write_ms_avg, r->nvs_write_ms_max);
    printf(" frames dropped during   %u\n", r->frames_dropped_during_nvs);

    printf("\n-- ENVIRONMENT ----------------------------------------\n");
    printf(" temperature             %.1f -> %.1f C\n",
           r->temp_c_start, r->temp_c_end);

    /* ---- verdict ----
     * Thresholds are the ones docs/ commits to. The spike says pass or fail
     * against them rather than leaving interpretation to whoever reads the
     * log, because "is 1.8% dropped frames fine?" is exactly the question
     * that gets answered optimistically at 11pm. */
    printf("\n-- VERDICT --------------------------------------------\n");

    bool ok = true;

    if (r->frames_rendered == 0) {
        printf(" FAIL   no frames rendered at all\n");
        ok = false;
    } else {
        double drop_pct = 100.0 * r->frames_dropped / r->frames_rendered;
        if (drop_pct > 0.1) {
            printf(" FAIL   dropped frames %.2f%% > 0.1%% budget\n", drop_pct);
            ok = false;
        } else {
            printf(" pass   dropped frames %.2f%%\n", drop_pct);
        }
    }

    if (r->fps_min < 55.0f) {
        printf(" FAIL   worst-window fps %.1f < 55\n", r->fps_min);
        ok = false;
    } else {
        printf(" pass   worst-window fps %.1f\n", r->fps_min);
    }

    /* docs/MEMORY.md §1 budgets ~200 KB of internal SRAM for the application
     * on top of WiFi and TLS. Less than 60 KB free at peak means the budget
     * is fiction. */
    if (r->internal_free_min < 60u * 1024u) {
        printf(" FAIL   internal SRAM free %.1f KB < 60 KB floor\n",
               KB(r->internal_free_min));
        ok = false;
    } else {
        printf(" pass   internal SRAM free %.1f KB\n", KB(r->internal_free_min));
    }

    if (r->jpeg_decode_ms_max > 400u) {
        printf(" FAIL   JPEG decode max %u ms > 400 ms "
               "(artwork would visibly lag a track change)\n",
               r->jpeg_decode_ms_max);
        ok = false;
    } else {
        printf(" pass   JPEG decode max %u ms\n", r->jpeg_decode_ms_max);
    }

    if (r->temp_c_end > 75.0f) {
        printf(" WARN   %.1f C - check enclosure ventilation (RISKS R12)\n",
               r->temp_c_end);
    }

    printf("\n %s\n", ok
        ? "R1 MITIGATED at these settings. Record in docs/MEMORY.md §1."
        : "R1 NOT MITIGATED. Do not freeze the architecture. Try, in order: "
          "larger bounce buffers -> 50 Hz refresh -> single framebuffer with "
          "partial refresh.");
    printf("========================================================\n\n");

    ESP_LOGI(TAG, "phase0 verdict: %s", ok ? "PASS" : "FAIL");
}
