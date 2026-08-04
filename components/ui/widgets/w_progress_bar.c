#include "w_progress_bar.h"

#include <string.h>

#include "intent_types.h"
#include "playback_intent_types.h"
#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"
#include "view_model_format.h"

/* THEME_TRACK_H, not the design spec's own "3px total" -- raised on real-
 * hardware feedback once the bar became a genuine scrub control rather
 * than a display: at 3px it was draggable in principle but hard to land
 * on, and visually inconsistent with the two volume tracks it sits above.
 * The spec wrote 3px for a bar that was never meant to be touched.
 *
 * The hit band grows with it, from 39px (3 drawn + 18 padding each side)
 * to a full THEME_TOUCH_MIN 56 (14 drawn + 21 each side) -- it was the one
 * control here that quietly fell short of the 56px rule, because the pad
 * was a hand-picked 18 rather than derived. Now it is derived, so the
 * band tracks the token instead of drifting from it. */
#define W_PROGRESS_BAR_HIT_PAD ((THEME_TOUCH_MIN - THEME_TRACK_H) / 2)
#define W_PROGRESS_BAR_TIME_MAX 8   /* "-120:45" + NUL -- matches VM_ELAPSED_MAX/VM_REMAINING_MAX,
                                        redeclared locally rather than pulling in the view model's
                                        header, same as w_volume_slider's own readout buffer */

/* permille (this slider's own 0..1000 range) -> absolute ms. int64 on the
 * way through because permille * duration_ms overflows uint32 for anything
 * past ~72 minutes -- a real case here, not a theoretical one (podcasts,
 * DJ sets). Shared by the drag preview and the seek submit so the position
 * the user was shown is exactly the one that gets sent. */
static uint32_t position_ms_from_permille(int32_t permille, uint32_t duration_ms)
{
    return (uint32_t)(((int64_t)permille * (int64_t)duration_ms) / 1000);
}

/* The timestamps follow the finger while scrubbing, instead of continuing
 * to report real playback. Without this the elapsed/remaining pair kept
 * ticking along with the track during a drag, so the one thing a scrubber
 * has to answer -- "what time am I about to land on?" -- was the one thing
 * it didn't show. The bar's own position was already drag-owned; only the
 * numbers weren't.
 *
 * Safe against a feedback loop: lv_bar_set_value()/lv_slider_set_value()
 * do not emit LV_EVENT_VALUE_CHANGED themselves (confirmed by reading
 * LVGL's lv_bar.c/lv_slider.c directly), so set_progress()'s own
 * programmatic writes below can never re-enter this. */
static void on_value_changed(lv_event_t *e)
{
    w_progress_bar_t *w = (w_progress_bar_t *)lv_event_get_user_data(e);

    if (w->duration_ms == 0) {
        return;   /* no real track length yet -- a percentage of nothing is not a time */
    }

    uint32_t position_ms = position_ms_from_permille(lv_slider_get_value(w->bar), w->duration_ms);

    char elapsed[W_PROGRESS_BAR_TIME_MAX];
    char remaining[W_PROGRESS_BAR_TIME_MAX];
    view_model_format_mmss(position_ms, elapsed, sizeof(elapsed));
    view_model_format_remaining_mmss(position_ms, w->duration_ms, remaining, sizeof(remaining));
    w_stroke_label_set_text(&w->elapsed_label, elapsed);
    w_stroke_label_set_text(&w->remaining_label, remaining);
}

static void on_released(lv_event_t *e)
{
    w_progress_bar_t *w = (w_progress_bar_t *)lv_event_get_user_data(e);

    if (w->intent_submit.submit == NULL || w->duration_ms == 0) {
        return;   /* not wired, or no real track length yet to seek within */
    }

    /* range is [0, 1000], set at create time */
    uint32_t position_ms = position_ms_from_permille(lv_slider_get_value(w->bar), w->duration_ms);

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
    lv_obj_set_size(w->bar, cfg->width, THEME_TRACK_H);
    lv_slider_set_range(w->bar, 0, 1000);   /* permille -- on_released() converts back to an
                                                 absolute ms position using duration_ms */
    lv_obj_set_ext_click_area(w->bar, W_PROGRESS_BAR_HIT_PAD);
    /* VALUE_CHANGED previews the drag (timestamps only, no intent);
     * RELEASED is what actually submits the seek. Deliberately split --
     * see this widget's own header comment on why a real Spotify seek call
     * must not fire per touch-move sample. */
    lv_obj_add_event_cb(w->bar, on_value_changed, LV_EVENT_VALUE_CHANGED, w);
    lv_obj_add_event_cb(w->bar, on_released, LV_EVENT_RELEASED, w);

    /* Round ends (LV_RADIUS_CIRCLE, so a pill), on both the track and the
     * fill below -- reverses this project's own earlier "square off the
     * tracks too" decision, asked for again once the bar became a real
     * drag control. LV_RADIUS_CIRCLE directly rather than a theme token,
     * per theme_metrics.h's own note about not aliasing LVGL constants.
     *
     * Deliberately NOT clip_corner: that is the property that hung LVGL's
     * layer allocator on w_art_card. Confirmed different code paths by
     * reading lv_refr.c -- the ARGB8888 lv_draw_layer_create() is reached
     * only when clip_corner is set AND the object has children, and an
     * lv_slider has no child objects. Plain radius never goes near it.
     *
     * It does have a real cost: radius > 0 drops out of lv_draw_sw_fill.c's
     * "most simple case" single-blend path into a per-draw lv_malloc plus
     * row-by-row mask application, and at 14px tall the corner bands
     * (rout = short_side >> 1 = 7, mirrored) cover every row, so there is
     * no fast middle section left. Measured rather than assumed -- see the
     * max-loop-duration figure in svc_diagnostics's DOMAIN_UI line. */
    lv_obj_set_style_radius(w->bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    /* Track styling copied from w_volume_slider's LV_PART_MAIN exactly, not
     * the design spec's own "solid oklch(0.08 0 0) track": translucent
     * rather than opaque, plus the 2px outline. Over full-bleed artwork the
     * opaque version read as a solid black bar while the volume track below
     * it let the art through and was outlined -- with both now the same
     * height and the same fill, the empty halves were the last thing
     * telling them apart. */
    lv_obj_set_style_bg_color(w->bar, THEME_COLOR_TEXT_STROKE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w->bar, 115, LV_PART_MAIN);   /* oklch(0.08 / 0.45) -- 0.45 * 255 ~= 115 */
    lv_obj_set_style_border_width(w->bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(w->bar, THEME_COLOR_TEXT_STROKE, LV_PART_MAIN);
    lv_obj_set_style_border_opa(w->bar, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_radius(w->bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);   /* matches the track
                                                                                 above -- a square
                                                                                 fill inside a pill
                                                                                 track shows its
                                                                                 corners at both ends */
    /* Same fill as w_volume_slider's track, deliberately -- the two sit
     * directly above each other on Now Playing, and once this one became a
     * real drag control they read as one family of controls rather than a
     * display plus a control. Replaces the design spec's own "white elapsed
     * fill at 34%" (THEME_COLOR_TEXT_ONIMAGE at opa 87), which was written
     * for a bar that was never meant to be touched. */
    lv_obj_set_style_bg_color(w->bar, THEME_COLOR_ACCENT_LIVE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->bar, LV_OPA_COVER, LV_PART_INDICATOR);

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
        /* All three no-op while the user's own finger is dragging -- the
         * real, tick-driven interpolated position would otherwise fight the
         * drag on every single frame, same reasoning as
         * w_volume_slider_set_value()'s own guard. The timestamps are inside
         * the guard too, not written unconditionally: mid-drag they belong
         * to on_value_changed() above, which derives them from the dragged
         * position so they answer "where am I about to land" rather than
         * continuing to report live playback. */
        lv_bar_set_value(w->bar, permille, LV_ANIM_OFF);   /* already interpolated every tick by
                                                                the caller -- no extra lv_anim layer */
        w_stroke_label_set_text(&w->elapsed_label, elapsed);
        w_stroke_label_set_text(&w->remaining_label, remaining);
    }
}
