#include "w_transport.h"

#include <string.h>

#include "denon_intent_types.h"
#include "intent_types.h"
#include "playback_intent_types.h"
#include "theme_colors.h"
#include "theme_metrics.h"

#define W_TRANSPORT_PLAY_PAUSE_SIZE 80
#define W_TRANSPORT_PREV_NEXT_TRI_W 16   /* per spec: "16 x 20px white CSS triangles" */
#define W_TRANSPORT_PREV_NEXT_TRI_H 20
#define W_TRANSPORT_PLAY_TRI_W       26   /* sized to roughly match the pause bars' own
                                              30px height for visual balance -- the spec
                                              doesn't give the play glyph its own explicit
                                              size, only the pause bars' */
#define W_TRANSPORT_PLAY_TRI_H       30
#define W_TRANSPORT_PAUSE_BAR_W       8
#define W_TRANSPORT_PAUSE_BAR_H      30
#define W_TRANSPORT_PAUSE_BAR_GAP    10

/* Small, shared, per-shape draw config -- one static instance per triangle
 * shape (prev/next/play all look identical across every w_transport
 * instance in this app), populated once. Mirrors w_stroke_label.c's own
 * ensure_stroke_style_inited() idiom for a shared, lazily-built constant. */
typedef struct {
    int32_t      w, h;
    bool           point_right;
    lv_color_t color;
} triangle_draw_cfg_t;

static triangle_draw_cfg_t s_prev_cfg;
static triangle_draw_cfg_t s_next_cfg;
static triangle_draw_cfg_t s_play_cfg;
static bool                    s_triangle_cfgs_inited = false;

static void ensure_triangle_cfgs_inited(void)
{
    if (s_triangle_cfgs_inited) {
        return;
    }
    s_prev_cfg = (triangle_draw_cfg_t){
        .w = W_TRANSPORT_PREV_NEXT_TRI_W, .h = W_TRANSPORT_PREV_NEXT_TRI_H,
        .point_right = false, .color = THEME_COLOR_TEXT_ONIMAGE,
    };
    s_next_cfg = (triangle_draw_cfg_t){
        .w = W_TRANSPORT_PREV_NEXT_TRI_W, .h = W_TRANSPORT_PREV_NEXT_TRI_H,
        .point_right = true, .color = THEME_COLOR_TEXT_ONIMAGE,
    };
    s_play_cfg = (triangle_draw_cfg_t){
        .w = W_TRANSPORT_PLAY_TRI_W, .h = W_TRANSPORT_PLAY_TRI_H,
        .point_right = true, .color = THEME_COLOR_BG,   /* dark triangle inside the white circle */
    };
    s_triangle_cfgs_inited = true;
}

/* 8-direction unit vectors, same N/S/E/W/NE/NW/SE/SW pattern and 1px
 * mono/meta-class offset w_stroke_label.c uses -- these triangles are
 * small icons at the same visual scale as that text, and the design
 * system's own principle ("every overlay carries its own black stroke for
 * contrast") applies just as much to hand-drawn shapes as to text; LVGL's
 * real drop-shadow mechanism isn't auto-wired for triangle draws
 * (confirmed: no lv_obj_init_draw_triangle_dsc exists), so this widget
 * uses the same proven offset-copy technique instead of a less-certain
 * manual drop-shadow-in-custom-draw path. */
#define W_TRANSPORT_TRI_STROKE_PX 1
static const int32_t s_tri_unit_dx[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
static const int32_t s_tri_unit_dy[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };

static void build_triangle_points(const triangle_draw_cfg_t *cfg, int32_t x0, int32_t y0,
                                     lv_point_precise_t p[3])
{
    if (cfg->point_right) {
        p[0] = (lv_point_precise_t){ x0 + cfg->w, y0 + cfg->h / 2 };
        p[1] = (lv_point_precise_t){ x0, y0 };
        p[2] = (lv_point_precise_t){ x0, y0 + cfg->h };
    } else {
        p[0] = (lv_point_precise_t){ x0, y0 + cfg->h / 2 };
        p[1] = (lv_point_precise_t){ x0 + cfg->w, y0 };
        p[2] = (lv_point_precise_t){ x0 + cfg->w, y0 + cfg->h };
    }
}

/* First low-level lv_draw_* call in this codebase's `ui` layer -- see this
 * widget's own header comment. obj->coords is absolute screen space (same
 * convention lv_switch.c's own LV_EVENT_DRAW_MAIN handler uses for its
 * knob rect), so the triangle's 3 points are computed directly from it,
 * not layer-relative. */
static void draw_triangle_cb(lv_event_t *e)
{
    lv_obj_t                    *obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t                  *layer = lv_event_get_layer(e);
    const triangle_draw_cfg_t *cfg   = (const triangle_draw_cfg_t *)lv_event_get_user_data(e);

    /* lv_obj_t's real struct layout is private to LVGL itself (only visible
     * inside the library's own widget .c files, e.g. lv_switch.c's own
     * LV_EVENT_DRAW_MAIN handler, which is where this obj->coords-is-
     * absolute-screen-space convention was confirmed) -- application/`ui`
     * code only ever sees the opaque public lv_obj_t, so coordinates come
     * from the real public accessor instead. */
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int32_t box_w = lv_area_get_width(&coords);
    int32_t box_h = lv_area_get_height(&coords);
    int32_t x0     = coords.x1 + (box_w - cfg->w) / 2;
    int32_t y0     = coords.y1 + (box_h - cfg->h) / 2;

    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.opa = LV_OPA_COVER;

    for (int i = 0; i < 8; i++) {
        dsc.color = THEME_COLOR_TEXT_STROKE;
        build_triangle_points(cfg, x0 + s_tri_unit_dx[i] * W_TRANSPORT_TRI_STROKE_PX,
                                 y0 + s_tri_unit_dy[i] * W_TRANSPORT_TRI_STROKE_PX, dsc.p);
        lv_draw_triangle(layer, &dsc);
    }

    dsc.color = cfg->color;
    build_triangle_points(cfg, x0, y0, dsc.p);
    lv_draw_triangle(layer, &dsc);
}

static void submit_kind(w_transport_t *w, playback_intent_kind_t kind)
{
    if (w->intent_submit.submit == NULL) {
        return;   /* not wired (e.g. a screen registered before app_wiring_ui.c
                      finished construction) -- fail silent, not a crash */
    }
    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_PLAYBACK;
    intent.kind   = (uint16_t)kind;
    w->intent_submit.submit(w->intent_submit.ctx, &intent);
}

/* Raw/unwrapped Denon submit -- same helper shape as w_power_glyph.c's own
 * submit_denon(), duplicated rather than shared because these are two
 * separate widgets with no existing common module to put it in and the
 * body is 5 lines; see w_transport.h's own intent_submit_raw comment for
 * why this domain bypasses the wrapped provider. */
static void submit_denon_raw(w_transport_t *w, denon_intent_kind_t kind, const denon_intent_payload_t *payload)
{
    if (w->intent_submit_raw.submit == NULL) {
        return;
    }
    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_DENON;
    intent.kind   = (uint16_t)kind;
    memcpy(intent.payload, payload, sizeof(*payload));
    w->intent_submit_raw.submit(w->intent_submit_raw.ctx, &intent);
}

static void on_prev_clicked(lv_event_t *e)
{
    submit_kind((w_transport_t *)lv_event_get_user_data(e), PLAYBACK_INTENT_PREVIOUS);
}

static void on_play_pause_clicked(lv_event_t *e)
{
    w_transport_t *w = (w_transport_t *)lv_event_get_user_data(e);

    if (w->is_playing) {
        submit_kind(w, PLAYBACK_INTENT_PAUSE);
        return;
    }

    if (!w->has_active_device) {
        /* Nothing is currently playing anywhere -- a plain PLAY intent
         * would 404 against Spotify's own /v1/me/player/play (no active
         * device to target) and surface as a generic "Couldn't reach
         * Spotify" rejection even though nothing is actually broken. Wake
         * the AVR the same way w_power_glyph's own "turn on" tap does
         * (power on + switch to the HEOS Music/NET input) and ask
         * svc_spotify to resume playback there -- see playback_intent_
         * types.h's own comment on PLAYBACK_INTENT_RESUME_ON_AVR for how
         * it resolves "whatever was playing before" without this widget
         * needing to know or remember what that was. */
        denon_intent_payload_t power_payload = { .power_on = true };
        submit_denon_raw(w, DENON_INTENT_SET_POWER, &power_payload);
        denon_intent_payload_t input_payload = { .input_id = DENON_INPUT_NET };
        submit_denon_raw(w, DENON_INTENT_SET_INPUT, &input_payload);
        submit_kind(w, PLAYBACK_INTENT_RESUME_ON_AVR);
        return;
    }

    submit_kind(w, PLAYBACK_INTENT_PLAY);
}

static void on_next_clicked(lv_event_t *e)
{
    submit_kind((w_transport_t *)lv_event_get_user_data(e), PLAYBACK_INTENT_NEXT);
}

static lv_obj_t *make_prev_next_hitbox(lv_obj_t *parent, const triangle_draw_cfg_t *tri_cfg)
{
    lv_obj_t *btn = lv_obj_create(parent);
    if (btn == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(btn, THEME_TOUCH_MIN, THEME_TOUCH_MIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, draw_triangle_cb, LV_EVENT_DRAW_MAIN, (void *)tri_cfg);
    return btn;
}

lv_obj_t *w_transport_create(lv_obj_t *parent, w_transport_t *w, const w_transport_cfg_t *cfg)
{
    ensure_triangle_cfgs_inited();

    w->intent_submit     = cfg->intent_submit;
    w->intent_submit_raw = cfg->intent_submit_raw;
    w->is_playing          = false;
    w->has_active_device = false;
    w->pending             = false;

    w->root = lv_obj_create(parent);
    if (w->root == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    /* Set explicitly here rather than relying on the w_transport_set_state()
     * call at the end of this function to establish it: that setter only
     * writes this property when `pending` actually changes, and it starts
     * out matching the w->pending = false initialized just above, so the
     * write would be (correctly) skipped. Stating the starting value here
     * keeps the row's opacity a property this widget owns explicitly, not
     * one inherited from whatever LVGL's default theme happens to leave. */
    lv_obj_set_style_opa(w->root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);
    lv_obj_set_style_pad_column(w->root, THEME_SPACE_M, 0);
    lv_obj_set_flex_flow(w->root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w->root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    /* Real bug, found live: this used to be LV_SIZE_CONTENT (content-sized,
     * i.e. narrower than the column) with the CALLER trying to re-center it
     * via lv_obj_set_align(LV_ALIGN_CENTER) -- which silently does nothing
     * for a flex child (the parent's flex algorithm still controls
     * position; lv_obj_set_align only takes effect once layout is bypassed
     * via LV_OBJ_FLAG_FLOATING). Simpler, correct fix: make this root
     * full-width like every other content-column child (title/artist/
     * progress/volume all already use lv_pct(100)) and let its OWN main-
     * axis CENTER alignment (set above) center the 3 buttons within that
     * full width -- lands on the panel's true optical centre, x = 240,
     * without fighting the parent's layout at all. */
    lv_obj_set_size(w->root, lv_pct(100), W_TRANSPORT_PLAY_PAUSE_SIZE);

    w->btn_prev = make_prev_next_hitbox(w->root, &s_prev_cfg);
    if (w->btn_prev == NULL) {
        return NULL;
    }
    lv_obj_add_event_cb(w->btn_prev, on_prev_clicked, LV_EVENT_CLICKED, w);

    w->btn_play_pause = lv_obj_create(w->root);
    if (w->btn_play_pause == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->btn_play_pause, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(w->btn_play_pause, W_TRANSPORT_PLAY_PAUSE_SIZE, W_TRANSPORT_PLAY_PAUSE_SIZE);
    lv_obj_set_style_radius(w->btn_play_pause, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(w->btn_play_pause, THEME_COLOR_TEXT_ONIMAGE, 0);
    lv_obj_set_style_bg_opa(w->btn_play_pause, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w->btn_play_pause, 2, 0);
    lv_obj_set_style_border_color(w->btn_play_pause, THEME_COLOR_TEXT_STROKE, 0);
    lv_obj_add_flag(w->btn_play_pause, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(w->btn_play_pause, on_play_pause_clicked, LV_EVENT_CLICKED, w);

    w->play_triangle = lv_obj_create(w->btn_play_pause);
    if (w->play_triangle == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->play_triangle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(w->play_triangle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(w->play_triangle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->play_triangle, 0, 0);
    lv_obj_set_size(w->play_triangle, W_TRANSPORT_PLAY_TRI_W, W_TRANSPORT_PLAY_TRI_H);
    lv_obj_center(w->play_triangle);
    lv_obj_add_event_cb(w->play_triangle, draw_triangle_cb, LV_EVENT_DRAW_MAIN, &s_play_cfg);

    w->pause_bar_l = lv_obj_create(w->btn_play_pause);
    w->pause_bar_r = lv_obj_create(w->btn_play_pause);
    if (w->pause_bar_l == NULL || w->pause_bar_r == NULL) {
        return NULL;
    }
    lv_obj_t *pause_bars[2] = { w->pause_bar_l, w->pause_bar_r };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *bar = pause_bars[i];
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, THEME_COLOR_BG, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_size(bar, W_TRANSPORT_PAUSE_BAR_W, W_TRANSPORT_PAUSE_BAR_H);
    }
    lv_obj_align(w->pause_bar_l, LV_ALIGN_CENTER, -(W_TRANSPORT_PAUSE_BAR_GAP / 2 + W_TRANSPORT_PAUSE_BAR_W / 2), 0);
    lv_obj_align(w->pause_bar_r, LV_ALIGN_CENTER, W_TRANSPORT_PAUSE_BAR_GAP / 2 + W_TRANSPORT_PAUSE_BAR_W / 2, 0);

    w->btn_next = make_prev_next_hitbox(w->root, &s_next_cfg);
    if (w->btn_next == NULL) {
        return NULL;
    }
    lv_obj_add_event_cb(w->btn_next, on_next_clicked, LV_EVENT_CLICKED, w);

    w_transport_set_state(w, false, false, false);

    return w->root;
}

void w_transport_set_state(w_transport_t *w, bool is_playing, bool pending, bool has_active_device)
{
    w->is_playing          = is_playing;
    w->has_active_device = has_active_device;

    /* The flag and state calls below are safe to make unconditionally --
     * lv_obj_add_flag()/remove_flag()/add_state()/remove_state() all check
     * whether the bit is already in the requested position and return
     * immediately if so (confirmed by reading lv_obj.c directly), so a
     * repeat call with an unchanged value costs a branch, not an
     * invalidation.
     *
     * lv_obj_set_style_opa() below does NOT have that property, which is
     * why `pending` is cached: it resolves to lv_obj_set_local_style_prop(),
     * which unconditionally writes the property and calls
     * lv_obj_refresh_style() (also confirmed directly, lv_obj_style.c) --
     * and LV_STYLE_OPA on a container propagates to every descendant, so
     * this one call invalidated and re-rendered the entire 480x80 transport
     * row (the white circle plus both 9-pass stroke-overdrawn triangles).
     * view_model_now_playing_apply_tick() calls this ~60x/second, and
     * `pending` is false almost all of that time, so that was a full-row
     * redraw every frame to re-apply the opacity it already had. */
    if (is_playing) {
        lv_obj_add_flag(w->play_triangle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(w->pause_bar_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(w->pause_bar_r, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(w->play_triangle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(w->pause_bar_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(w->pause_bar_r, LV_OBJ_FLAG_HIDDEN);
    }

    if (pending != w->pending) {
        w->pending = pending;
        lv_obj_set_style_opa(w->root, pending ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
    if (pending) {
        lv_obj_add_state(w->btn_prev, LV_STATE_DISABLED);
        lv_obj_add_state(w->btn_play_pause, LV_STATE_DISABLED);
        lv_obj_add_state(w->btn_next, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(w->btn_prev, LV_STATE_DISABLED);
        lv_obj_remove_state(w->btn_play_pause, LV_STATE_DISABLED);
        lv_obj_remove_state(w->btn_next, LV_STATE_DISABLED);
    }
}
