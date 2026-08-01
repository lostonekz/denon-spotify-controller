#include "w_transport.h"

#include "intent_types.h"
#include "playback_intent_types.h"
#include "theme_colors.h"
#include "theme_metrics.h"

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

static void on_prev_clicked(lv_event_t *e)
{
    submit_kind((w_transport_t *)lv_event_get_user_data(e), PLAYBACK_INTENT_PREVIOUS);
}

static void on_play_pause_clicked(lv_event_t *e)
{
    w_transport_t *w = (w_transport_t *)lv_event_get_user_data(e);
    submit_kind(w, w->is_playing ? PLAYBACK_INTENT_PAUSE : PLAYBACK_INTENT_PLAY);
}

static void on_next_clicked(lv_event_t *e)
{
    submit_kind((w_transport_t *)lv_event_get_user_data(e), PLAYBACK_INTENT_NEXT);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *symbol, lv_obj_t **out_label)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, THEME_TOUCH_MIN, THEME_TOUCH_MIN);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, THEME_COLOR_SURFACE_HIGH, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(label);
    if (out_label != NULL) {
        *out_label = label;
    }
    return btn;
}

lv_obj_t *w_transport_create(lv_obj_t *parent, w_transport_t *w, const w_transport_cfg_t *cfg)
{
    w->intent_submit = cfg->intent_submit;
    w->is_playing     = false;

    w->root = lv_obj_create(parent);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);
    lv_obj_set_style_pad_column(w->root, THEME_SPACE_L, 0);
    lv_obj_set_flex_flow(w->root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w->root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(w->root, LV_SIZE_CONTENT, THEME_TOUCH_MIN);

    w->btn_prev = make_button(w->root, LV_SYMBOL_PREV, NULL);
    lv_obj_add_event_cb(w->btn_prev, on_prev_clicked, LV_EVENT_CLICKED, w);

    w->btn_play_pause = make_button(w->root, LV_SYMBOL_PLAY, &w->play_pause_label);
    lv_obj_add_event_cb(w->btn_play_pause, on_play_pause_clicked, LV_EVENT_CLICKED, w);

    w->btn_next = make_button(w->root, LV_SYMBOL_NEXT, NULL);
    lv_obj_add_event_cb(w->btn_next, on_next_clicked, LV_EVENT_CLICKED, w);

    return w->root;
}

void w_transport_set_state(w_transport_t *w, bool is_playing, bool pending)
{
    w->is_playing = is_playing;
    lv_label_set_text(w->play_pause_label, is_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    lv_obj_set_style_opa(w->root, pending ? LV_OPA_50 : LV_OPA_COVER, 0);
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
