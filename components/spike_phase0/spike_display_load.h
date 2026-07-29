/* Phase 0 display load. THROWAWAY. Private to spike_phase0. */
#ifndef SPIKE_DISPLAY_LOAD_H
#define SPIKE_DISPLAY_LOAD_H

#include <stdint.h>

#include "esp_err.h"

typedef struct spike_display_s spike_display_t;

esp_err_t spike_display_init(spike_display_t **out);
void      spike_display_render_frame(spike_display_t *d, uint32_t phase);
void      spike_display_stats(const spike_display_t *d,
                              uint32_t *frames, uint32_t *drops,
                              uint32_t *longest_us, float *fps_min);

#endif /* SPIKE_DISPLAY_LOAD_H */
