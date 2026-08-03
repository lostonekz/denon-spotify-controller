#include "w_marquee_label.h"

lv_obj_t *w_marquee_label_create(lv_obj_t *parent, w_marquee_label_t *w, const w_marquee_label_cfg_t *cfg)
{
    /* Hardcoded, not threaded through w_marquee_label_cfg_t -- this widget's
     * only real use in this codebase is the hero title, which the design
     * system specifies a 2px stroke for; no caller needs a different value. */
    w_stroke_label_cfg_t stroke_cfg = { .font = cfg->font, .color = cfg->color, .width = cfg->width, .offset_px = 2 };
    lv_obj_t                *root      = w_stroke_label_create(parent, &w->stroke, &stroke_cfg);
    if (root == NULL) {
        return NULL;
    }

    for (int i = 0; i < W_STROKE_LABEL_COPY_COUNT; i++) {
        lv_label_set_long_mode(w->stroke.stroke[i], LV_LABEL_LONG_MODE_SCROLL);
    }
    lv_label_set_long_mode(w->stroke.fg, LV_LABEL_LONG_MODE_SCROLL);

    w->label = w->stroke.fg;
    return root;
}

void w_marquee_label_set_text(w_marquee_label_t *w, const char *text)
{
    w_stroke_label_set_text(&w->stroke, text);
}
