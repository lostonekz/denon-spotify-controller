#include "w_volume_slider.h"

#include <string.h>

#include "denon_intent_types.h"
#include "intent_types.h"
#include "multi_intent_types.h"
#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"
#include "esp_log.h"

#include "view_model_format.h"

#define W_VOLUME_SLIDER_TRACK_H  14
#define W_VOLUME_SLIDER_HIT_H    56
#define W_VOLUME_SLIDER_READOUT_MAX 8   /* matches VM_VOLUME_DISPLAY_MAX -- "98.5" + NUL */

static const char *TAG = "w_volume_slider";

static void on_value_changed(lv_event_t *e)
{
    w_volume_slider_t *w = (w_volume_slider_t *)lv_event_get_user_data(e);
    uint16_t                half_db = (uint16_t)lv_slider_get_value(w->slider);

    /* Was two ESP_LOGW "DIAG:" lines (one here, one on the submit result
     * below), left over from the Tier 7 live investigation into why volume
     * drags did nothing. LV_EVENT_VALUE_CHANGED fires continuously while a
     * finger is down, and every log line costs the full svc_logging pipeline
     * (vsnprintf + redaction scan + ring write) plus a blocking ~10ms UART
     * write at 115200 -- on the UI task, mid-gesture, which is exactly the
     * stall R10's loop-duration assertion exists to catch. Demoted to DEBUG,
     * not deleted: CONFIG_LOG_MAXIMUM_LEVEL_DEBUG is on (sdkconfig.defaults),
     * so this is one runtime log-level change away if this path ever needs
     * investigating again, at zero steady-state cost. */
    ESP_LOGD(TAG, "on_value_changed: half_db=%u submit=%p", half_db, (void *)w->intent_submit.submit);

    char text[W_VOLUME_SLIDER_READOUT_MAX];
    view_model_format_half_db(half_db, text, sizeof(text));
    w_stroke_label_set_text(&w->label_readout, text);

    if (w->intent_submit.submit == NULL) {
        return;   /* not wired -- fail silent, matching w_transport's own contract */
    }
    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_MULTI;
    intent.kind   = (uint16_t)MULTI_INTENT_SET_VOLUME;
    multi_intent_payload_t payload = { .volume_half_db = half_db };
    memcpy(intent.payload, &payload, sizeof(payload));
    bool ok = w->intent_submit.submit(w->intent_submit.ctx, &intent);
    ESP_LOGD(TAG, "submit() returned %d", (int)ok);
}

lv_obj_t *w_volume_slider_create(lv_obj_t *parent, w_volume_slider_t *w, const w_volume_slider_cfg_t *cfg)
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
    lv_obj_set_size(w->row, LV_PCT(100), W_VOLUME_SLIDER_HIT_H);

    w_stroke_label_cfg_t vol_cfg = {
        .font = &ui_font_meta, .color = THEME_COLOR_TEXT_ONIMAGE,
        .width = LV_SIZE_CONTENT, .offset_px = 1,
    };
    if (w_stroke_label_create(w->row, &w->label_vol, &vol_cfg) == NULL) {
        return NULL;
    }
    w_stroke_label_set_text(&w->label_vol, "VOL");

    w->slider = lv_slider_create(w->row);
    if (w->slider == NULL) {
        return NULL;
    }
    lv_obj_set_flex_grow(w->slider, 1);
    lv_obj_set_height(w->slider, W_VOLUME_SLIDER_TRACK_H);
    lv_slider_set_range(w->slider, (int32_t)DENON_MV_HALF_DB_MIN, (int32_t)DENON_MV_HALF_DB_MAX);
    lv_obj_set_ext_click_area(w->slider, (W_VOLUME_SLIDER_HIT_H - W_VOLUME_SLIDER_TRACK_H) / 2);

    lv_obj_set_style_radius(w->slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w->slider, THEME_COLOR_TEXT_STROKE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w->slider, 115, LV_PART_MAIN);   /* oklch(0.08 / 0.45) -- 0.45 * 255 ~= 115 */
    lv_obj_set_style_border_width(w->slider, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(w->slider, THEME_COLOR_TEXT_STROKE, LV_PART_MAIN);
    lv_obj_set_style_border_opa(w->slider, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_radius(w->slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(w->slider, THEME_COLOR_ACCENT_LIVE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->slider, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(w->slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(w->slider, 0, LV_PART_KNOB);   /* no knob -- "drag anywhere along it" */

    lv_obj_add_event_cb(w->slider, on_value_changed, LV_EVENT_VALUE_CHANGED, w);

    w_stroke_label_cfg_t readout_cfg = {
        .font = &ui_font_meta_lg, .color = THEME_COLOR_TEXT_ONIMAGE,
        .width = LV_SIZE_CONTENT, .offset_px = 1,
    };
    if (w_stroke_label_create(w->row, &w->label_readout, &readout_cfg) == NULL) {
        return NULL;
    }

    return w->row;
}

void w_volume_slider_set_value(w_volume_slider_t *w, uint16_t volume_half_db, const char *display_text)
{
    if (!lv_obj_has_state(w->slider, LV_STATE_PRESSED)) {
        lv_slider_set_value(w->slider, (int32_t)volume_half_db, LV_ANIM_OFF);
    }
    w_stroke_label_set_text(&w->label_readout, display_text);
}
