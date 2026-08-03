#include "w_info_row.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"

#define W_INFO_ROW_DOT_SIZE 8

static lv_color_t color_for_tone(tone_t tone)
{
    switch (tone) {
    case TONE_LIVE:   return THEME_COLOR_ACCENT_LIVE;
    case TONE_STALE:  return THEME_COLOR_STALE;
    case TONE_OFFLINE:
    default:            return THEME_COLOR_DANGER;
    }
}

lv_obj_t *w_info_row_create(lv_obj_t *parent, w_info_row_t *w, const w_info_row_cfg_t *cfg)
{
    w->root = lv_obj_create(parent);
    if (w->root == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);
    lv_obj_set_flex_flow(w->root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_width(w->root, lv_pct(100));
    lv_obj_set_height(w->root, LV_SIZE_CONTENT);

    w->left_group = lv_obj_create(w->root);
    if (w->left_group == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->left_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(w->left_group, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(w->left_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->left_group, 0, 0);
    lv_obj_set_style_pad_all(w->left_group, 0, 0);
    lv_obj_set_style_pad_column(w->left_group, THEME_SPACE_XS, 0);
    lv_obj_set_flex_flow(w->left_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w->left_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_width(w->left_group, LV_SIZE_CONTENT);
    lv_obj_set_height(w->left_group, LV_SIZE_CONTENT);

    if (cfg->show_status_dot) {
        w->dot = lv_obj_create(w->left_group);
        if (w->dot == NULL) {
            return NULL;
        }
        lv_obj_remove_flag(w->dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(w->dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(w->dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(w->dot, 0, 0);
        lv_obj_set_size(w->dot, W_INFO_ROW_DOT_SIZE, W_INFO_ROW_DOT_SIZE);
    } else {
        w->dot = NULL;
    }

    w->label = lv_label_create(w->left_group);
    if (w->label == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(w->label, &ui_font_label, 0);
    lv_obj_set_style_text_color(w->label, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(w->label, cfg->label);

    w->value = lv_label_create(w->root);
    if (w->value == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->value, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_grow(w->value, 1);
    lv_obj_set_style_text_font(w->value, &ui_font_label, 0);
    lv_obj_set_style_text_color(w->value, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(w->value, LV_TEXT_ALIGN_RIGHT, 0);

    if (cfg->show_status_dot) {
        w_info_row_set_status(w, "", TONE_OFFLINE);
    } else {
        w_info_row_set_value(w, "");
    }

    return w->root;
}

void w_info_row_set_value(w_info_row_t *w, const char *value)
{
    lv_obj_set_style_text_color(w->value, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(w->value, value);
}

void w_info_row_set_status(w_info_row_t *w, const char *value, tone_t tone)
{
    lv_color_t color = color_for_tone(tone);
    if (w->dot != NULL) {
        lv_obj_set_style_bg_color(w->dot, color, 0);
        lv_obj_set_style_bg_opa(w->dot, LV_OPA_COVER, 0);
    }
    lv_obj_set_style_text_color(w->value, color, 0);
    lv_label_set_text(w->value, value);
}
