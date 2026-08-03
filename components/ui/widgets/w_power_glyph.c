#include "w_power_glyph.h"

#include <string.h>

#include "esp_log.h"

#include "denon_intent_types.h"
#include "intent_types.h"
#include "theme_colors.h"

static const char *TAG = "w_power_glyph";

#define W_POWER_GLYPH_SIZE       20
#define W_POWER_GLYPH_RING_W      3
#define W_POWER_GLYPH_NOTCH_W     3
#define W_POWER_GLYPH_NOTCH_H     9
#define W_POWER_GLYPH_NOTCH_DY   -3   /* margin-top: -3px per spec -- shifts the notch up so
                                          it straddles the ring's own top border stroke */

static void submit_denon(w_power_glyph_t *w, denon_intent_kind_t kind, const denon_intent_payload_t *payload)
{
    if (w->intent_submit.submit == NULL) {
        ESP_LOGW(TAG, "DIAG: submit_denon kind=%d -- intent_submit.submit is NULL, not wired", (int)kind);
        return;   /* not wired -- fail silent, matching w_transport's own contract */
    }
    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_DENON;
    intent.kind   = (uint16_t)kind;
    memcpy(intent.payload, payload, sizeof(*payload));
    bool ok = w->intent_submit.submit(w->intent_submit.ctx, &intent);
    ESP_LOGW(TAG, "DIAG: submit_denon kind=%d submitted, ok=%d", (int)kind, (int)ok);
}

static void on_glyph_clicked(lv_event_t *e)
{
    w_power_glyph_t *w = (w_power_glyph_t *)lv_event_get_user_data(e);

    ESP_LOGW(TAG, "DIAG: on_glyph_clicked fired, last_power_on=%d", (int)w->last_power_on);

    bool turning_on = !w->last_power_on;

    denon_intent_payload_t power_payload = { .power_on = turning_on };
    denon_intent_payload_t input_payload = { .input_id = turning_on ? DENON_INPUT_NET : DENON_INPUT_TV };

    /* Real bug, found live: "turn on" worked (powered on + switched source),
     * but "turn off" silently did not. The two commands aren't symmetric on
     * the wire -- DENON_INTENT_SET_INPUT encodes to Denon's telnet "SI"
     * (select-input) command (denon_codec_tables.c's SI_ROWS table,
     * confirmed directly), and Denon's own external-control protocol
     * documents "SI" as auto-powering the main zone ON if it's currently in
     * standby. Sending SET_POWER(off) THEN SET_INPUT(TV) -- the previous,
     * uniform order used for both directions -- meant the input command
     * immediately re-powered the unit right after the power command had
     * just turned it off, through denon_cmdq's own guaranteed in-order FIFO
     * delivery (never reordered/dropped), so both commands really did
     * arrive, in that order, with the second one undoing the first. Turning
     * on has no such conflict (power-on then select-input both move toward
     * "on"), so only the off direction needs reordering: select the input
     * first (a normal SI call while still powered on, no side effect to
     * worry about) and send the power-off command last, so it's the final,
     * authoritative word on power state with nothing queued after it to
     * contradict it. */
    if (turning_on) {
        submit_denon(w, DENON_INTENT_SET_POWER, &power_payload);
        submit_denon(w, DENON_INTENT_SET_INPUT, &input_payload);
    } else {
        submit_denon(w, DENON_INTENT_SET_INPUT, &input_payload);
        submit_denon(w, DENON_INTENT_SET_POWER, &power_payload);
    }
}

lv_obj_t *w_power_glyph_create(lv_obj_t *parent, w_power_glyph_t *w, const w_power_glyph_cfg_t *cfg)
{
    w->intent_submit  = cfg->intent_submit;
    w->last_power_on = false;

    w->root = lv_obj_create(parent);
    if (w->root == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    /* The notch pokes W_POWER_GLYPH_NOTCH_DY (3px) above root's own top edge
     * -- same clipping lesson as w_stroke_label.c: without this flag that
     * overflow is silently clipped, cutting the top of the notch off. */
    lv_obj_add_flag(w->root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);
    lv_obj_set_size(w->root, W_POWER_GLYPH_SIZE, W_POWER_GLYPH_SIZE);
    lv_obj_add_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(w->root, on_glyph_clicked, LV_EVENT_CLICKED, w);
    /* THEME_TOUCH_MIN (56px) worth of hit area around a visually small 20px
     * glyph -- the same draw-smaller-than-tappable rule the design system
     * applies to every other control (page-edge tab, transport icons). */
    lv_obj_set_ext_click_area(w->root, (56 - W_POWER_GLYPH_SIZE) / 2);

    w->ring = lv_obj_create(w->root);
    if (w->ring == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->ring, LV_OBJ_FLAG_SCROLLABLE);
    /* Real bug, found live: every plain lv_obj_create() object is
     * CLICKABLE by default (lv_obj.c: obj->flags = LV_OBJ_FLAG_CLICKABLE
     * at creation) -- left on, this child (which visually covers nearly
     * all of w->root's tap area) silently swallows every tap before it
     * reaches w->root's own LV_EVENT_CLICKED handler, since LVGL's
     * hit-test picks the most specific (child) object under the touch
     * point and CLICKED does not bubble to parents by default. This is
     * why the glyph rendered correctly but never actually responded to a
     * tap. */
    lv_obj_remove_flag(w->ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(w->ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(w->ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->ring, W_POWER_GLYPH_RING_W, 0);
    lv_obj_set_size(w->ring, W_POWER_GLYPH_SIZE, W_POWER_GLYPH_SIZE);
    lv_obj_align(w->ring, LV_ALIGN_TOP_LEFT, 0, 0);
    /* Silhouette-outline against unpredictable album art -- LVGL 9's real
     * drop-shadow mechanism (confirmed auto-wired for rect/border draw),
     * not a custom draw callback. One shadow, not the spec's literal two
     * stacked filters -- see this widget's header comment. */
    lv_obj_set_style_drop_shadow_radius(w->ring, 2, 0);
    lv_obj_set_style_drop_shadow_color(w->ring, THEME_COLOR_TEXT_STROKE, 0);
    lv_obj_set_style_drop_shadow_opa(w->ring, LV_OPA_COVER, 0);

    w->notch = lv_obj_create(w->root);
    if (w->notch == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->notch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(w->notch, LV_OBJ_FLAG_CLICKABLE);   /* same real bug as w->ring above */
    lv_obj_set_style_radius(w->notch, 0, 0);
    lv_obj_set_style_border_width(w->notch, 0, 0);
    lv_obj_set_size(w->notch, W_POWER_GLYPH_NOTCH_W, W_POWER_GLYPH_NOTCH_H);
    lv_obj_align(w->notch, LV_ALIGN_TOP_MID, 0, W_POWER_GLYPH_NOTCH_DY);
    lv_obj_set_style_drop_shadow_radius(w->notch, 2, 0);
    lv_obj_set_style_drop_shadow_color(w->notch, THEME_COLOR_TEXT_STROKE, 0);
    lv_obj_set_style_drop_shadow_opa(w->notch, LV_OPA_COVER, 0);

    w_power_glyph_set_state(w, false, TONE_OFFLINE);

    return w->root;
}

void w_power_glyph_set_state(w_power_glyph_t *w, bool power_on, tone_t denon_tone)
{
    w->last_power_on = power_on;

    lv_color_t color;
    if (!power_on) {
        color = THEME_COLOR_TEXT_TERTIARY;                        /* gray -- off */
    } else if (denon_tone == TONE_LIVE) {
        color = THEME_COLOR_ACCENT_LIVE;                          /* green -- on, confirmed healthy */
    } else {
        color = THEME_COLOR_STALE;                                /* amber -- on, link uncertain */
    }

    lv_obj_set_style_border_color(w->ring, color, 0);
    lv_obj_set_style_bg_color(w->notch, color, 0);
    lv_obj_set_style_bg_opa(w->notch, LV_OPA_COVER, 0);
}
