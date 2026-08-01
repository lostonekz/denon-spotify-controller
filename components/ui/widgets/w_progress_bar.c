#include "w_progress_bar.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"

#define W_PROGRESS_BAR_HEIGHT 4   /* UI_ARCHITECTURE.md's Art-forward Context: "thin 4 px progress bar" */

lv_obj_t *w_progress_bar_create(lv_obj_t *parent, w_progress_bar_t *w, const w_progress_bar_cfg_t *cfg)
{
    w->root = lv_obj_create(parent);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);
    lv_obj_set_style_pad_row(w->root, THEME_SPACE_XS, 0);
    lv_obj_set_flex_flow(w->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(w->root, cfg->width, LV_SIZE_CONTENT);

    w->bar = lv_bar_create(w->root);
    lv_obj_set_size(w->bar, cfg->width, W_PROGRESS_BAR_HEIGHT);
    lv_bar_set_range(w->bar, 0, 1000);
    lv_obj_set_style_bg_color(w->bar, THEME_COLOR_SURFACE_HIGH, 0);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w->bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(w->bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    lv_obj_t *time_row = lv_obj_create(w->root);
    lv_obj_remove_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(time_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(time_row, 0, 0);
    lv_obj_set_style_pad_all(time_row, 0, 0);
    lv_obj_set_size(time_row, cfg->width, LV_SIZE_CONTENT);

    w->elapsed_label = lv_label_create(time_row);
    lv_obj_set_style_text_font(w->elapsed_label, &ui_font_meta, 0);
    lv_obj_set_style_text_color(w->elapsed_label, THEME_COLOR_TEXT_TERTIARY, 0);
    lv_obj_align(w->elapsed_label, LV_ALIGN_LEFT_MID, 0, 0);

    w->remaining_label = lv_label_create(time_row);
    lv_obj_set_style_text_font(w->remaining_label, &ui_font_meta, 0);
    lv_obj_set_style_text_color(w->remaining_label, THEME_COLOR_TEXT_TERTIARY, 0);
    lv_obj_align(w->remaining_label, LV_ALIGN_RIGHT_MID, 0, 0);

    return w->root;
}

void w_progress_bar_set_progress(w_progress_bar_t *w, uint16_t permille, const char *elapsed,
                                    const char *remaining, lv_color_t accent)
{
    lv_bar_set_value(w->bar, permille, LV_ANIM_OFF);   /* already interpolated every tick by
                                                            the caller -- no extra lv_anim layer */
    lv_obj_set_style_bg_color(w->bar, accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_label_set_text(w->elapsed_label, elapsed);
    lv_label_set_text(w->remaining_label, remaining);
}
