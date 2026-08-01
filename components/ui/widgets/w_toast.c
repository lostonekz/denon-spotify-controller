#include "w_toast.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"

#define W_TOAST_DISMISS_MS 3000   /* dwell time -- not one of THEME_MOTION_*'s
                                      animation durations, a content-visibility
                                      duration instead */
#define W_TOAST_MAX_WIDTH   360

static void on_dismiss_timer(lv_timer_t *t)
{
    w_toast_t *w = (w_toast_t *)lv_timer_get_user_data(t);
    lv_obj_add_flag(w->root, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *w_toast_create(lv_obj_t *parent, w_toast_t *w)
{
    w->root = lv_obj_create(parent);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(w->root, THEME_COLOR_SURFACE_HIGH, 0);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w->root, THEME_RADIUS_CARD, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_left(w->root, THEME_SPACE_M, 0);
    lv_obj_set_style_pad_right(w->root, THEME_SPACE_M, 0);
    lv_obj_set_style_pad_top(w->root, THEME_SPACE_S, 0);
    lv_obj_set_style_pad_bottom(w->root, THEME_SPACE_S, 0);
    lv_obj_set_width(w->root, W_TOAST_MAX_WIDTH);
    lv_obj_set_height(w->root, LV_SIZE_CONTENT);
    lv_obj_align(w->root, LV_ALIGN_BOTTOM_MID, 0, -THEME_SPACE_XL);

    w->label = lv_label_create(w->root);
    lv_obj_set_style_text_font(w->label, &ui_font_body, 0);
    lv_obj_set_style_text_color(w->label, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_width(w->label, LV_PCT(100));
    lv_label_set_long_mode(w->label, LV_LABEL_LONG_MODE_WRAP);

    w->dismiss_timer = lv_timer_create(on_dismiss_timer, W_TOAST_DISMISS_MS, w);
    lv_timer_set_repeat_count(w->dismiss_timer, 1);
    lv_timer_set_auto_delete(w->dismiss_timer, false);   /* reused by every show(), never
                                                              re-created -- see w_toast_show() */
    lv_timer_pause(w->dismiss_timer);

    lv_obj_add_flag(w->root, LV_OBJ_FLAG_HIDDEN);
    return w->root;
}

void w_toast_show(w_toast_t *w, const char *message)
{
    lv_label_set_text(w->label, message);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_HIDDEN);

    lv_timer_set_repeat_count(w->dismiss_timer, 1);
    lv_timer_reset(w->dismiss_timer);
    lv_timer_resume(w->dismiss_timer);
}
