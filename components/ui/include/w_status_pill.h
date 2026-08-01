/*
 * Small connectivity/link indicator (UI_ARCHITECTURE.md §4) -- "the honest
 * STALE/OFFLINE surface": hidden entirely when tone == TONE_LIVE, otherwise
 * a short label in THEME_COLOR_STALE/DANGER. Screen §6: "When tone ==
 * STALE, text drops to secondary and a w_status_pill appears. Content
 * stays visible."
 */
#ifndef W_STATUS_PILL_H
#define W_STATUS_PILL_H

#include "lvgl.h"

#include "view_model_tone.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *root;
    lv_obj_t *label;
} w_status_pill_t;

/* No cfg -- nothing to configure at create time, everything arrives via
 * w_status_pill_set_tone(). Hidden by default until the first set_tone()
 * call. */
lv_obj_t *w_status_pill_create(lv_obj_t *parent, w_status_pill_t *w);

void w_status_pill_set_tone(w_status_pill_t *w, tone_t tone);

#ifdef __cplusplus
}
#endif

#endif /* W_STATUS_PILL_H */
