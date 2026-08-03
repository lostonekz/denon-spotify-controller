#include "w_volume_cap_slider.h"

#include <string.h>

#include "denon_intent_types.h"
#include "intent_types.h"
#include "settings_intent_types.h"
#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"

#include "view_model_format.h"

#define W_VOLUME_CAP_SLIDER_TRACK_H  14
#define W_VOLUME_CAP_SLIDER_HIT_H    56
#define W_VOLUME_CAP_SLIDER_READOUT_MAX 8   /* matches VM_..._VOLUME_DISPLAY_MAX -- "98.5" + NUL */

static void on_value_changed(lv_event_t *e)
{
    w_volume_cap_slider_t *w = (w_volume_cap_slider_t *)lv_event_get_user_data(e);
    uint16_t                    half_db = (uint16_t)lv_slider_get_value(w->slider);

    char text[W_VOLUME_CAP_SLIDER_READOUT_MAX];
    view_model_format_half_db(half_db, text, sizeof(text));
    lv_label_set_text(w->label_readout, text);

    if (w->intent_submit.submit == NULL) {
        return;   /* not wired -- fail silent, matching w_volume_slider's own contract */
    }
    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_SETTINGS;
    intent.kind   = (uint16_t)SETTINGS_INTENT_SET_VOLUME_LIMIT;
    settings_intent_payload_t payload = { .volume_limit_half_db = half_db };
    memcpy(intent.payload, &payload, sizeof(payload));
    w->intent_submit.submit(w->intent_submit.ctx, &intent);
}

lv_obj_t *w_volume_cap_slider_create(lv_obj_t *parent, w_volume_cap_slider_t *w,
                                        const w_volume_cap_slider_cfg_t *cfg)
{
    w->intent_submit = cfg->intent_submit;

    w->row = lv_obj_create(parent);
    if (w->row == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(w->row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->row, 0, 0);
    lv_obj_set_style_pad_all(w->row, 0, 0);
    lv_obj_set_style_pad_column(w->row, THEME_SPACE_M, 0);
    lv_obj_set_flex_flow(w->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w->row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(w->row, LV_PCT(100), W_VOLUME_CAP_SLIDER_HIT_H);

    w->label_caption = lv_label_create(w->row);
    if (w->label_caption == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->label_caption, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(w->label_caption, &ui_font_label, 0);
    lv_obj_set_style_text_color(w->label_caption, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(w->label_caption, "Don't go past");

    w->slider = lv_slider_create(w->row);
    if (w->slider == NULL) {
        return NULL;
    }
    lv_obj_set_flex_grow(w->slider, 1);
    lv_obj_set_height(w->slider, W_VOLUME_CAP_SLIDER_TRACK_H);
    lv_slider_set_range(w->slider, (int32_t)DENON_MV_HALF_DB_MIN, (int32_t)DENON_MV_HALF_DB_MAX);
    lv_obj_set_ext_click_area(w->slider, (W_VOLUME_CAP_SLIDER_HIT_H - W_VOLUME_CAP_SLIDER_TRACK_H) / 2);

    lv_obj_set_style_radius(w->slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w->slider, THEME_COLOR_TEXT_STROKE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w->slider, 115, LV_PART_MAIN);
    lv_obj_set_style_border_width(w->slider, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(w->slider, THEME_COLOR_TEXT_STROKE, LV_PART_MAIN);
    lv_obj_set_style_border_opa(w->slider, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_radius(w->slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(w->slider, THEME_COLOR_STALE, LV_PART_INDICATOR);   /* amber, not
                                                                                       green -- this
                                                                                       constrains
                                                                                       rather than
                                                                                       plays */
    lv_obj_set_style_bg_opa(w->slider, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(w->slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(w->slider, 0, LV_PART_KNOB);

    lv_obj_add_event_cb(w->slider, on_value_changed, LV_EVENT_VALUE_CHANGED, w);

    w->label_readout = lv_label_create(w->row);
    if (w->label_readout == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->label_readout, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(w->label_readout, &ui_font_meta_lg, 0);
    lv_obj_set_style_text_color(w->label_readout, THEME_COLOR_STALE, 0);

    return w->row;
}

void w_volume_cap_slider_set_value(w_volume_cap_slider_t *w, uint16_t volume_limit_half_db,
                                       const char *display_text)
{
    if (!lv_obj_has_state(w->slider, LV_STATE_PRESSED)) {
        lv_slider_set_value(w->slider, (int32_t)volume_limit_half_db, LV_ANIM_OFF);
    }
    lv_label_set_text(w->label_readout, display_text);
}
