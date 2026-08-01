#include "w_marquee_label.h"

lv_obj_t *w_marquee_label_create(lv_obj_t *parent, w_marquee_label_t *w, const w_marquee_label_cfg_t *cfg)
{
    w->label = lv_label_create(parent);
    lv_obj_set_style_text_font(w->label, cfg->font, 0);
    lv_obj_set_style_text_color(w->label, cfg->color, 0);
    lv_obj_set_width(w->label, cfg->width);
    lv_label_set_long_mode(w->label, LV_LABEL_LONG_MODE_SCROLL);
    return w->label;
}

void w_marquee_label_set_text(w_marquee_label_t *w, const char *text)
{
    lv_label_set_text(w->label, text);
}
