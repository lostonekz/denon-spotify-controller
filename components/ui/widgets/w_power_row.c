#include "w_power_row.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"

#define W_POWER_ROW_HEIGHT       72
#define W_POWER_ROW_SWITCH_W     44
#define W_POWER_ROW_SWITCH_H     24
#define W_POWER_ROW_KNOB_SIZE    20
#define W_POWER_ROW_KNOB_INSET   2

static void on_row_clicked(lv_event_t *e)
{
    w_power_row_t *w = (w_power_row_t *)lv_event_get_user_data(e);
    /* The glyph's own 56px hit area (a child of root) already handles taps
     * that land on it -- LVGL's CLICKED event does not bubble to parents by
     * default, so this handler only ever fires for taps elsewhere on the
     * row, never twice for the same tap. Both paths converge on the same
     * real toggle -- see w_power_glyph_toggle()'s own comment. */
    w_power_glyph_toggle(&w->glyph);
}

lv_obj_t *w_power_row_create(lv_obj_t *parent, w_power_row_t *w, const w_power_row_cfg_t *cfg)
{
    w->root = lv_obj_create(parent);
    if (w->root == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(w->root, THEME_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(w->root, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, THEME_SPACE_M, 0);
    lv_obj_set_style_pad_column(w->root, THEME_SPACE_M, 0);
    lv_obj_set_flex_flow(w->root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(w->root, LV_PCT(100), W_POWER_ROW_HEIGHT);
    lv_obj_add_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(w->root, on_row_clicked, LV_EVENT_CLICKED, w);

    w_power_glyph_cfg_t glyph_cfg = { .intent_submit = cfg->intent_submit };
    if (w_power_glyph_create(w->root, &w->glyph, &glyph_cfg) == NULL) {
        return NULL;
    }

    w->label = lv_label_create(w->root);
    if (w->label == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_grow(w->label, 1);
    lv_obj_set_style_text_font(w->label, &ui_font_label, 0);
    lv_obj_set_style_text_color(w->label, THEME_COLOR_TEXT_PRIMARY, 0);

    w->switch_track = lv_obj_create(w->root);
    if (w->switch_track == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->switch_track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(w->switch_track, LV_OBJ_FLAG_CLICKABLE);   /* decorative only -- see
                                                                       this file's own header
                                                                       comment on why this
                                                                       never initiates a toggle */
    lv_obj_set_style_radius(w->switch_track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(w->switch_track, 0, 0);
    lv_obj_set_style_pad_all(w->switch_track, 0, 0);
    lv_obj_set_size(w->switch_track, W_POWER_ROW_SWITCH_W, W_POWER_ROW_SWITCH_H);

    w->switch_knob = lv_obj_create(w->switch_track);
    if (w->switch_knob == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->switch_knob, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(w->switch_knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(w->switch_knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(w->switch_knob, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_bg_opa(w->switch_knob, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w->switch_knob, 0, 0);
    lv_obj_set_size(w->switch_knob, W_POWER_ROW_KNOB_SIZE, W_POWER_ROW_KNOB_SIZE);

    w_power_row_set_state(w, false, TONE_OFFLINE, "Connecting...");

    return w->root;
}

void w_power_row_set_state(w_power_row_t *w, bool power_on, tone_t denon_tone, const char *power_label)
{
    w_power_glyph_set_state(&w->glyph, power_on, denon_tone);

    lv_label_set_text(w->label, power_label);

    lv_color_t track_color = power_on
        ? (denon_tone == TONE_LIVE ? THEME_COLOR_ACCENT_LIVE : THEME_COLOR_STALE)
        : THEME_COLOR_SURFACE_HIGH;
    lv_obj_set_style_bg_color(w->switch_track, track_color, 0);
    lv_obj_set_style_bg_opa(w->switch_track, LV_OPA_COVER, 0);

    lv_obj_align(w->switch_knob, power_on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
                 power_on ? -W_POWER_ROW_KNOB_INSET : W_POWER_ROW_KNOB_INSET, 0);
}
