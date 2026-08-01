#include "w_status_pill.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"

lv_obj_t *w_status_pill_create(lv_obj_t *parent, w_status_pill_t *w)
{
    w->root = lv_obj_create(parent);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(w->root, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_left(w->root, THEME_SPACE_S, 0);
    lv_obj_set_style_pad_right(w->root, THEME_SPACE_S, 0);
    lv_obj_set_style_pad_top(w->root, THEME_SPACE_XS, 0);
    lv_obj_set_style_pad_bottom(w->root, THEME_SPACE_XS, 0);
    lv_obj_set_size(w->root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    w->label = lv_label_create(w->root);
    lv_obj_set_style_text_font(w->label, &ui_font_label, 0);
    lv_obj_set_style_text_color(w->label, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(w->label);

    lv_obj_add_flag(w->root, LV_OBJ_FLAG_HIDDEN);   /* TONE_LIVE default -- nothing to say yet */
    return w->root;
}

void w_status_pill_set_tone(w_status_pill_t *w, tone_t tone)
{
    switch (tone) {
    case TONE_LIVE:
        lv_obj_add_flag(w->root, LV_OBJ_FLAG_HIDDEN);
        return;
    case TONE_STALE:
        lv_obj_set_style_bg_color(w->root, THEME_COLOR_STALE, 0);
        lv_label_set_text(w->label, "STALE");
        break;
    case TONE_OFFLINE:
        lv_obj_set_style_bg_color(w->root, THEME_COLOR_DANGER, 0);
        lv_label_set_text(w->label, "OFFLINE");
        break;
    }
    lv_obj_set_style_bg_opa(w->root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_HIDDEN);
}
