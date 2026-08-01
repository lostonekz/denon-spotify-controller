/*
 * Interpolated progress + elapsed/remaining (UI_ARCHITECTURE.md §4).
 * Display-only in Phase 5 -- no drag/scrub (Phase 5 plan §6: "no scrub UI
 * exists until Phase 5" was Phase 4's own SEEK-cut comment, and Phase 5
 * still has no seek intent kind to scrub toward; this is a materially
 * smaller scope than "fully realised" and is called out explicitly here,
 * not silently shipped half-working).
 */
#ifndef W_PROGRESS_BAR_H
#define W_PROGRESS_BAR_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t width;
} w_progress_bar_cfg_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *bar;
    lv_obj_t *elapsed_label;
    lv_obj_t *remaining_label;
} w_progress_bar_t;

lv_obj_t *w_progress_bar_create(lv_obj_t *parent, w_progress_bar_t *w, const w_progress_bar_cfg_t *cfg);

/* permille: 0..1000 (now_playing_vm_t.progress_permille). elapsed/
 * remaining: already-formatted strings (view_model_format_mmss()/
 * view_model_format_remaining_mmss()). accent: the fill color -- §5.2's
 * per-track dynamic accent, clamped for contrast by view_model_now_playing_
 * build() already, not re-clamped here. */
void w_progress_bar_set_progress(w_progress_bar_t *w, uint16_t permille, const char *elapsed,
                                    const char *remaining, lv_color_t accent);

#ifdef __cplusplus
}
#endif

#endif /* W_PROGRESS_BAR_H */
