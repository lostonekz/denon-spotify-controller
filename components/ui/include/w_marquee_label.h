/*
 * Text that gently scrolls only when it overflows, pauses at each end
 * (UI_ARCHITECTURE.md §4) -- a thin wrapper over LVGL's own
 * LV_LABEL_LONG_MODE_SCROLL ("roll the text back and forth"), which
 * already does exactly this natively; no hand-rolled animation needed.
 */
#ifndef W_MARQUEE_LABEL_H
#define W_MARQUEE_LABEL_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const lv_font_t *font;
    lv_color_t        color;
    int32_t              width;   /* fixed width the label scrolls within */
} w_marquee_label_cfg_t;

typedef struct {
    lv_obj_t *label;
} w_marquee_label_t;

lv_obj_t *w_marquee_label_create(lv_obj_t *parent, w_marquee_label_t *w, const w_marquee_label_cfg_t *cfg);

void w_marquee_label_set_text(w_marquee_label_t *w, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* W_MARQUEE_LABEL_H */
