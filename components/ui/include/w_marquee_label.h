/*
 * Text that gently scrolls only when it overflows, pauses at each end
 * (UI_ARCHITECTURE.md §4) -- a thin wrapper over LVGL's own
 * LV_LABEL_LONG_MODE_SCROLL ("roll the text back and forth"), which
 * already does exactly this natively; no hand-rolled animation needed.
 *
 * Composes w_stroke_label (black outline, see its own header comment) --
 * SCROLL mode is set on all 5 of its labels (4 stroke copies + 1 fg), so
 * they scroll in lockstep for the common case (title fits, no scrolling
 * happens at all). A genuinely overflowing title could in principle drift
 * out of sync between the 5 independent LVGL scroll animations -- not
 * observed, not specifically guarded against, a real but low-priority
 * follow-up if it ever shows up on real hardware.
 */
#ifndef W_MARQUEE_LABEL_H
#define W_MARQUEE_LABEL_H

#include <stdint.h>

#include "lvgl.h"

#include "w_stroke_label.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const lv_font_t *font;
    lv_color_t        color;
    int32_t              width;   /* fixed width the label scrolls within */
} w_marquee_label_cfg_t;

typedef struct {
    w_stroke_label_t stroke;
    lv_obj_t *label;   /* == stroke.fg -- kept as its own field since this was
                           the widget's original public field name */
} w_marquee_label_t;

lv_obj_t *w_marquee_label_create(lv_obj_t *parent, w_marquee_label_t *w, const w_marquee_label_cfg_t *cfg);

void w_marquee_label_set_text(w_marquee_label_t *w, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* W_MARQUEE_LABEL_H */
