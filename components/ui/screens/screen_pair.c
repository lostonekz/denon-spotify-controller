#include "screen_pair.h"

#include <string.h>

#include "esp_log.h"

#include "pairing_intent_types.h"
#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"
#include "view_model_pair.h"

static const char *TAG = "screen_pair";
#define DIAG_FAIL(step) ESP_LOGE(TAG, "%s returned NULL -- LVGL object allocation failed", step)

/* 240 px / 29 modules = a scale of 8 for the version-3 codes a ~33-char URL
 * produces, comfortably above w_qr's own minimum. The white card adds
 * THEME_SPACE_XL of quiet zone on each side, so the card is 304 px -- most of
 * a 480 px panel, which is what makes it scannable from arm's length. */
#define SCREEN_PAIR_QR_PX  240

static void submit_pairing(screen_pair_ctx_t *sc, pairing_intent_kind_t kind, uint8_t force)
{
    if (sc->intent_submit_raw.submit == NULL) {
        return;
    }
    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_PAIRING;
    intent.kind   = (uint16_t)kind;
    pairing_intent_payload_t payload = { .force = force };
    memcpy(intent.payload, &payload, sizeof(payload));
    sc->intent_submit_raw.submit(sc->intent_submit_raw.ctx, &intent);
}

static void on_close_clicked(lv_event_t *e)
{
    screen_pair_ctx_t *sc = (screen_pair_ctx_t *)lv_event_get_user_data(e);
    if (sc->on_close != NULL) {
        sc->on_close(sc->on_close_ctx);
    }
}

static void on_confirm_clicked(lv_event_t *e)
{
    screen_pair_ctx_t *sc = (screen_pair_ctx_t *)lv_event_get_user_data(e);
    /* Logged so a "the button does nothing" report can be told apart from a
     * "the tap never landed" one from the serial log alone -- the first
     * version of this button really did nothing, and without this line the
     * two were indistinguishable from the outside. */
    ESP_LOGI(TAG, "forced re-pair requested from the panel");
    /* force=1: unpair first. The FSM has no AUTHORIZED->PAIRING edge, so
     * without this both the pair request AND the code paste that follows it
     * would return success and do nothing at all. */
    submit_pairing(sc, PAIRING_INTENT_START, 1);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_obj_create(parent);
    if (btn == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(btn, THEME_COLOR_SURFACE_HIGH, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, THEME_RADIUS_CARD, 0);
    lv_obj_set_style_pad_hor(btn, THEME_SPACE_L, 0);
    lv_obj_set_height(btn, THEME_TOUCH_MIN);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(btn);
    if (label == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(label, &ui_font_label, 0);
    lv_obj_set_style_text_color(label, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *pair_create(screen_ctx_t *ctx)
{
    screen_pair_ctx_t *sc = (screen_pair_ctx_t *)ctx->user_ctx;

    lv_obj_t *root = lv_obj_create(ctx->parent);
    if (root == NULL) {
        DIAG_FAIL("pair_create root");
        return NULL;
    }
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    /* Opaque, square-cornered, untransformed -- these three are what let
     * lv_refr_get_top_obj() pick this object and skip drawing all four
     * screens underneath. A scrim or a rounded corner here silently gives
     * that back. See ui_router.h's header. */
    lv_obj_set_style_bg_color(root, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, THEME_SPACE_L, 0);
    lv_obj_set_style_pad_row(root, THEME_SPACE_M, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    sc->headline_label = lv_label_create(root);
    if (sc->headline_label == NULL) {
        DIAG_FAIL("headline_label");
        return NULL;
    }
    lv_obj_remove_flag(sc->headline_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(sc->headline_label, &ui_font_title, 0);
    lv_obj_set_style_text_color(sc->headline_label, THEME_COLOR_TEXT_PRIMARY, 0);

    if (w_qr_create(root, &sc->qr, SCREEN_PAIR_QR_PX) == NULL) {
        DIAG_FAIL("w_qr_create");
        return NULL;
    }

    sc->url_label = lv_label_create(root);
    if (sc->url_label == NULL) {
        DIAG_FAIL("url_label");
        return NULL;
    }
    lv_obj_remove_flag(sc->url_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(sc->url_label, &ui_font_meta, 0);
    lv_obj_set_style_text_color(sc->url_label, THEME_COLOR_TEXT_SECONDARY, 0);

    sc->detail_label = lv_label_create(root);
    if (sc->detail_label == NULL) {
        DIAG_FAIL("detail_label");
        return NULL;
    }
    lv_obj_remove_flag(sc->detail_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(sc->detail_label, &ui_font_label, 0);
    lv_obj_set_style_text_color(sc->detail_label, THEME_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_width(sc->detail_label, lv_pct(90));
    lv_label_set_long_mode(sc->detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(sc->detail_label, LV_TEXT_ALIGN_CENTER, 0);

    sc->confirm_btn = make_button(root, "Reconnect a different account", on_confirm_clicked, sc);
    if (sc->confirm_btn == NULL) {
        DIAG_FAIL("confirm_btn");
        return NULL;
    }

    sc->close_btn = make_button(root, "Close", on_close_clicked, sc);
    if (sc->close_btn == NULL) {
        DIAG_FAIL("close_btn");
        return NULL;
    }

    return root;
}

/* The listener's whole lifetime. Nothing else starts or stops it. */
static void pair_mount(screen_ctx_t *ctx)
{
    submit_pairing((screen_pair_ctx_t *)ctx->user_ctx, PAIRING_INTENT_START, 0);
}

static void pair_unmount(screen_ctx_t *ctx)
{
    submit_pairing((screen_pair_ctx_t *)ctx->user_ctx, PAIRING_INTENT_STOP, 0);
}

static void pair_destroy(screen_ctx_t *ctx) { (void)ctx; }

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* lv_label_set_text() always frees and re-mallocs the string then
 * invalidates -- it does not self-diff. Same gate w_info_row.c documents. */
static void set_text_gated(lv_obj_t *label, const char *text)
{
    const char *current = lv_label_get_text(label);
    if (current == NULL || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void pair_update(screen_ctx_t *ctx, const void *vm)
{
    screen_pair_ctx_t *sc = (screen_pair_ctx_t *)ctx->user_ctx;
    const pair_vm_t     *u  = (const pair_vm_t *)vm;

    set_text_gated(sc->headline_label, u->headline);
    set_text_gated(sc->detail_label, u->detail);

    /* If the code itself failed to render, the typed URL below it is the only
     * way in -- so it stays visible in exactly that case, not hidden
     * alongside the card. */
    bool qr_shown = u->show_qr && w_qr_set_text(&sc->qr, u->url);
    set_hidden(sc->qr.card, !qr_shown);
    set_hidden(sc->url_label, !u->show_qr);
    if (u->show_qr) {
        set_text_gated(sc->url_label, u->url);
    }

    set_hidden(sc->confirm_btn, !u->show_confirm_button);
}

screen_if_t screen_pair_get_interface(void)
{
    screen_if_t iface = {
        .create   = pair_create,
        .mount    = pair_mount,
        .update   = pair_update,
        .unmount  = pair_unmount,
        .destroy  = pair_destroy,
    };
    return iface;
}
