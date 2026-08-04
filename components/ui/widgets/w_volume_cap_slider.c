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

/* Zone boundaries, in the half-dB units this slider's own range is in
 * (DENON_MV_HALF_DB_MIN..MAX, 0..197 == 0.0..98.5 dB). Expressed here as
 * the dB figures a user actually reads off the readout, doubled -- 35.0 dB
 * is this project's own default volume limit (SETTING_VOLUME_LIMIT_MAX),
 * so "green" is exactly "at or below the cap we already consider safe".
 * The whole fill takes one zone's colour rather than showing the
 * boundaries as segments -- a deliberate choice: it needs no extra objects
 * and no per-tick geometry, which matters on this render path (RISKS.md
 * R1). */
#define W_VOLUME_CAP_GREEN_MAX_HALF_DB   (35u * 2u)   /* <= 35.0 dB */
#define W_VOLUME_CAP_ORANGE_MAX_HALF_DB  (68u * 2u)   /* <= 68.0 dB */

static lv_color_t zone_color(uint16_t half_db)
{
    if (half_db <= W_VOLUME_CAP_GREEN_MAX_HALF_DB) {
        return THEME_COLOR_ACCENT_LIVE;
    }
    if (half_db <= W_VOLUME_CAP_ORANGE_MAX_HALF_DB) {
        return THEME_COLOR_STALE;
    }
    return THEME_COLOR_DANGER;
}

/* Both the fill and the numeral take the zone colour, so they can never
 * disagree about which zone the cap is in. Change-gated on the raw value
 * because this runs from a per-tick setter AND from every drag sample --
 * lv_obj_set_style_*() has no internal no-op check (see
 * w_source_status_card.c's own note). */
static void apply_zone_color(w_volume_cap_slider_t *w, uint16_t half_db)
{
    if (w->applied_zone && w->applied_zone_half_db == half_db) {
        return;
    }
    w->applied_zone          = true;
    w->applied_zone_half_db = half_db;

    lv_color_t c = zone_color(half_db);
    lv_obj_set_style_bg_color(w->slider, c, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(w->label_readout, c, 0);
}

static void on_value_changed(lv_event_t *e)
{
    w_volume_cap_slider_t *w = (w_volume_cap_slider_t *)lv_event_get_user_data(e);
    uint16_t                    half_db = (uint16_t)lv_slider_get_value(w->slider);

    char text[W_VOLUME_CAP_SLIDER_READOUT_MAX];
    view_model_format_half_db(half_db, text, sizeof(text));
    lv_label_set_text(w->label_readout, text);
    /* Recolour from the dragged value, not the (lagging) stored one, so the
     * zone changes under the user's finger as they cross a boundary. */
    apply_zone_color(w, half_db);

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
    w->applied_zone  = false;   /* first set_value()/drag always writes the colour */

    /* A column: the "Don't go past" caption on its own line, then a bar row
     * built to exactly mirror w_volume_slider's own (same VOL lead-in text/
     * font, same THEME_SPACE_M gaps, same 56px hit height, same readout
     * font) -- so the track itself lands at an identical width and x
     * position on every screen that has one. The caption is above rather
     * than inline precisely because "Don't go past" is far wider than "VOL"
     * and would otherwise squeeze this screen's track narrower than the
     * others'. Bottom-anchored by the caller (screen_connection.c), and the
     * bar row is this column's last child, so the track's own vertical
     * position matches the other screens' too, not just its width. */
    w->row = lv_obj_create(parent);
    if (w->row == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(w->row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->row, 0, 0);
    lv_obj_set_style_pad_all(w->row, 0, 0);
    lv_obj_set_style_pad_row(w->row, THEME_SPACE_XS, 0);
    lv_obj_set_flex_flow(w->row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w->row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_width(w->row, LV_PCT(100));
    lv_obj_set_height(w->row, LV_SIZE_CONTENT);

    w->label_caption = lv_label_create(w->row);
    if (w->label_caption == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->label_caption, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(w->label_caption, &ui_font_label, 0);
    lv_obj_set_style_text_color(w->label_caption, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(w->label_caption, "Don't go past");

    lv_obj_t *bar_row = lv_obj_create(w->row);
    if (bar_row == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(bar_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(bar_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar_row, 0, 0);
    lv_obj_set_style_pad_all(bar_row, 0, 0);
    lv_obj_set_style_pad_column(bar_row, THEME_SPACE_M, 0);
    lv_obj_set_flex_flow(bar_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(bar_row, LV_PCT(100), W_VOLUME_CAP_SLIDER_HIT_H);

    /* Same text and font as w_volume_slider's own lead-in -- this is what
     * holds the track's left inset identical across screens. Not stored on
     * the widget: set once here, never updated. */
    lv_obj_t *label_vol = lv_label_create(bar_row);
    if (label_vol == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(label_vol, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(label_vol, &ui_font_meta, 0);
    lv_obj_set_style_text_color(label_vol, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(label_vol, "VOL");

    w->slider = lv_slider_create(bar_row);
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
    /* Fill colour is not fixed here -- apply_zone_color() sets it from the
     * cap's own value (green/amber/red by zone). */
    lv_obj_set_style_bg_opa(w->slider, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(w->slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(w->slider, 0, LV_PART_KNOB);

    lv_obj_add_event_cb(w->slider, on_value_changed, LV_EVENT_VALUE_CHANGED, w);

    w->label_readout = lv_label_create(bar_row);
    if (w->label_readout == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->label_readout, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(w->label_readout, &ui_font_meta_lg, 0);
    /* Text colour, like the fill above, comes from apply_zone_color(). */

    return w->row;
}

void w_volume_cap_slider_set_value(w_volume_cap_slider_t *w, uint16_t volume_limit_half_db,
                                       const char *display_text)
{
    if (!lv_obj_has_state(w->slider, LV_STATE_PRESSED)) {
        lv_slider_set_value(w->slider, (int32_t)volume_limit_half_db, LV_ANIM_OFF);
        lv_label_set_text(w->label_readout, display_text);
        apply_zone_color(w, volume_limit_half_db);
    }
    /* Mid-drag, both the numeral and the zone colour are owned by
     * on_value_changed() above, driven by the dragged value -- overwriting
     * them here with the (still-lagging) stored one would make the readout
     * and fill flicker between the finger's value and the old one on every
     * tick until the round trip completes. */
}
