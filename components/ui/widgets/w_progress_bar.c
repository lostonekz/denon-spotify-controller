#include "w_progress_bar.h"

#include <string.h>

#include "intent_types.h"
#include "playback_intent_types.h"
#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"

#define W_PROGRESS_BAR_HEIGHT   3   /* design spec: "3px total" */
#define W_PROGRESS_BAR_HIT_PAD 18   /* extends the real touch area well beyond the 3px visual --
                                        same "draw smaller than tappable" rule as every other
                                        control here, via ext_click_area (doesn't affect layout) */

static void on_released(lv_event_t *e)
{
    w_progress_bar_t *w = (w_progress_bar_t *)lv_event_get_user_data(e);

    if (w->intent_submit.submit == NULL || w->duration_ms == 0) {
        return;   /* not wired, or no real track length yet to seek within */
    }

    int32_t permille = lv_slider_get_value(w->bar);   /* range is [0, 1000], set at create time */
    uint32_t position_ms = (uint32_t)(((int64_t)permille * (int64_t)w->duration_ms) / 1000);

    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_PLAYBACK;
    intent.kind   = (uint16_t)PLAYBACK_INTENT_SEEK;
    playback_intent_payload_t payload = { .seek_position_ms = position_ms };
    memcpy(intent.payload, &payload, sizeof(payload));
    w->intent_submit.submit(w->intent_submit.ctx, &intent);
}

lv_obj_t *w_progress_bar_create(lv_obj_t *parent, w_progress_bar_t *w, const w_progress_bar_cfg_t *cfg)
{
    w->intent_submit = cfg->intent_submit;
    w->duration_ms      = 0;

    w->root = lv_obj_create(parent);
    if (w->root == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);
    lv_obj_set_style_pad_row(w->root, THEME_SPACE_XS, 0);
    lv_obj_set_flex_flow(w->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(w->root, cfg->width, LV_SIZE_CONTENT);

    /* lv_slider, not lv_bar -- see this widget's own header comment for why
     * (lv_bar has no touch input of its own; lv_slider already proved out
     * the "drag anywhere, no visible knob" idiom for w_volume_slider). */
    w->bar = lv_slider_create(w->root);
    if (w->bar == NULL) {
        return NULL;
    }
    lv_obj_set_size(w->bar, cfg->width, W_PROGRESS_BAR_HEIGHT);
    lv_slider_set_range(w->bar, 0, 1000);   /* permille -- on_released() converts back to an
                                                 absolute ms position using duration_ms */
    lv_obj_set_ext_click_area(w->bar, W_PROGRESS_BAR_HIT_PAD);
    lv_obj_add_event_cb(w->bar, on_released, LV_EVENT_RELEASED, w);

    lv_obj_set_style_radius(w->bar, 0, 0);                    /* square track -- user's own
                                                                    locked answer, overriding
                                                                    the spec's 2px */
    lv_obj_set_style_bg_color(w->bar, THEME_COLOR_TEXT_STROKE, 0);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, 0);          /* "solid oklch(0.08 0 0) track" */

    lv_obj_set_style_radius(w->bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(w->bar, THEME_COLOR_TEXT_ONIMAGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->bar, 87, LV_PART_INDICATOR);   /* "white elapsed fill at 34%" --
                                                                    0.34 * 255 ~= 87 */

    lv_obj_set_style_bg_opa(w->bar, LV_OPA_TRANSP, LV_PART_KNOB);   /* no knob -- "drag anywhere
                                                                         along it", same as
                                                                         w_volume_slider */
    lv_obj_set_style_pad_all(w->bar, 0, LV_PART_KNOB);

    lv_obj_t *time_row = lv_obj_create(w->root);
    if (time_row == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(time_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(time_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(time_row, 0, 0);
    lv_obj_set_style_pad_all(time_row, 0, 0);
    lv_obj_set_size(time_row, cfg->width, LV_SIZE_CONTENT);

    w_stroke_label_cfg_t elapsed_cfg = {
        .font = &ui_font_meta, .color = THEME_COLOR_TEXT_ONIMAGE, .width = LV_SIZE_CONTENT, .offset_px = 1,
    };
    if (w_stroke_label_create(time_row, &w->elapsed_label, &elapsed_cfg) == NULL) {
        return NULL;
    }
    lv_obj_align(w->elapsed_label.root, LV_ALIGN_LEFT_MID, 0, 0);

    w_stroke_label_cfg_t remaining_cfg = {
        .font = &ui_font_meta, .color = THEME_COLOR_TEXT_ONIMAGE, .width = LV_SIZE_CONTENT, .offset_px = 1,
    };
    if (w_stroke_label_create(time_row, &w->remaining_label, &remaining_cfg) == NULL) {
        return NULL;
    }
    lv_obj_align(w->remaining_label.root, LV_ALIGN_RIGHT_MID, 0, 0);

    return w->root;
}

void w_progress_bar_set_progress(w_progress_bar_t *w, uint16_t permille, const char *elapsed,
                                     const char *remaining, uint32_t duration_ms)
{
    w->duration_ms = duration_ms;
    if (!lv_obj_has_state(w->bar, LV_STATE_PRESSED)) {
        /* no-op while the user's own finger is dragging it -- otherwise the
         * real, tick-driven interpolated position would fight the drag on
         * every single frame, same reasoning as w_volume_slider_set_value()'s
         * own guard. */
        lv_bar_set_value(w->bar, permille, LV_ANIM_OFF);   /* already interpolated every tick by
                                                                the caller -- no extra lv_anim layer */
    }
    w_stroke_label_set_text(&w->elapsed_label, elapsed);
    w_stroke_label_set_text(&w->remaining_label, remaining);
}
