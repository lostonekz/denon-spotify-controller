#include "w_info_row.h"

#include <string.h>

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
    w->applied = false;   /* nothing applied yet -- the first set_value/set_status
                              call must write whatever colour it carries */

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

/* Shared by both public setters below, and change-gated on both halves:
 * the System and Connection screens' apply glue calls these on every
 * ui_app_task iteration (~60x/second) for values that change at most once
 * a second and usually not at all (firmware version, panel id, an IP
 * address).
 *
 * Neither underlying LVGL call self-diffs -- lv_obj_set_style_*() always
 * writes the property and refreshes the style, and lv_label_set_text()
 * always lv_free()s and re-lv_malloc()s the string, then invalidates
 * (confirmed in LVGL's own lv_obj_style.c / lv_label.c). With a full
 * screen of these rows, ungated that was a continuous stream of heap
 * churn and redraws for unchanged text.
 *
 * The colour is compared with lv_color_eq() rather than cached as the
 * tone_t it came from, so the one caller that passes a fixed colour
 * (set_value, always THEME_COLOR_TEXT_PRIMARY) and the one that derives
 * it from a tone share exactly the same gate, with no second
 * representation of "what is currently applied" to keep in sync. */
static void info_row_apply(w_info_row_t *w, lv_color_t color, const char *value)
{
    if (!w->applied || !lv_color_eq(w->applied_color, color)) {
        w->applied       = true;
        w->applied_color = color;
        if (w->dot != NULL) {
            lv_obj_set_style_bg_color(w->dot, color, 0);
            lv_obj_set_style_bg_opa(w->dot, LV_OPA_COVER, 0);
        }
        lv_obj_set_style_text_color(w->value, color, 0);
    }

    if (value != NULL) {
        const char *current = lv_label_get_text(w->value);
        if (current == NULL || strcmp(current, value) != 0) {
            lv_label_set_text(w->value, value);
        }
    }
}

void w_info_row_set_value(w_info_row_t *w, const char *value)
{
    info_row_apply(w, THEME_COLOR_TEXT_PRIMARY, value);
}

void w_info_row_set_status(w_info_row_t *w, const char *value, tone_t tone)
{
    info_row_apply(w, color_for_tone(tone), value);
}
