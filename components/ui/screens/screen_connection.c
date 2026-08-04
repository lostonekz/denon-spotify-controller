#include "screen_connection.h"

#include "esp_log.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"
#include "view_model_connection.h"

/* Same null-check-and-propagate discipline as screen_denon.c's own
 * DIAG_FAIL. */
static const char *TAG = "screen_connection";
#define DIAG_FAIL(step) ESP_LOGE(TAG, "%s returned NULL -- LVGL object allocation failed", step)

static lv_obj_t *make_receiver_card(lv_obj_t *parent, screen_connection_ctx_t *cc)
{
    lv_obj_t *card = lv_obj_create(parent);
    if (card == NULL) {
        DIAG_FAIL("receiver card");
        return NULL;
    }
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(card, THEME_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(card, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, THEME_SPACE_M, 0);
    lv_obj_set_style_pad_row(card, THEME_SPACE_M, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);

    w_info_row_cfg_t host_cfg = { .label = "Receiver", .show_status_dot = false };
    if (w_info_row_create(card, &cc->row_host, &host_cfg) == NULL) {
        DIAG_FAIL("row_host");
        return NULL;
    }

    w_info_row_cfg_t rt_cfg = { .label = "Round trip", .show_status_dot = false };
    if (w_info_row_create(card, &cc->row_round_trip, &rt_cfg) == NULL) {
        DIAG_FAIL("row_round_trip");
        return NULL;
    }

    return card;
}

static lv_obj_t *make_transport_cards_row(lv_obj_t *parent, screen_connection_ctx_t *cc)
{
    lv_obj_t *row = lv_obj_create(parent);
    if (row == NULL) {
        DIAG_FAIL("transport cards row");
        return NULL;
    }
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, THEME_SPACE_M, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);

    w_source_status_card_cfg_t telnet_cfg = { .icon_kind = W_SOURCE_STATUS_CARD_ICON_NONE, .title = "Telnet" };
    if (w_source_status_card_create(row, &cc->telnet_card, &telnet_cfg) == NULL) {
        DIAG_FAIL("telnet_card");
        return NULL;
    }

    w_source_status_card_cfg_t http_cfg = { .icon_kind = W_SOURCE_STATUS_CARD_ICON_NONE, .title = "HTTP" };
    if (w_source_status_card_create(row, &cc->http_card, &http_cfg) == NULL) {
        DIAG_FAIL("http_card");
        return NULL;
    }

    return row;
}

/* Mirrors screen_denon.c's own make_volume_row() exactly, so the volume-cap
 * slider lands at the identical absolute screen position as every other
 * volume control (user request) -- root carries no padding, this wrap owns
 * THEME_SPACE_L side padding and a THEME_SPACE_S bottom offset. */
static lv_obj_t *make_volume_cap_row(lv_obj_t *parent, screen_connection_ctx_t *cc)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    if (wrap == NULL) {
        DIAG_FAIL("volume cap row wrap");
        return NULL;
    }
    lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_set_style_pad_left(wrap, THEME_SPACE_L, 0);
    lv_obj_set_style_pad_right(wrap, THEME_SPACE_L, 0);
    lv_obj_set_width(wrap, lv_pct(100));
    lv_obj_set_height(wrap, LV_SIZE_CONTENT);
    lv_obj_align(wrap, LV_ALIGN_BOTTOM_MID, 0, -THEME_SPACE_S);

    w_volume_cap_slider_cfg_t cap_cfg = { .intent_submit = cc->intent_submit_raw };
    if (w_volume_cap_slider_create(wrap, &cc->volume_cap, &cap_cfg) == NULL) {
        DIAG_FAIL("volume_cap");
        return NULL;
    }
    lv_obj_set_width(cc->volume_cap.row, lv_pct(100));

    return wrap;
}

static lv_obj_t *connection_create(screen_ctx_t *ctx)
{
    screen_connection_ctx_t *cc = (screen_connection_ctx_t *)ctx->user_ctx;

    lv_obj_t *root = lv_obj_create(ctx->parent);
    if (root == NULL) {
        DIAG_FAIL("connection_create root");
        return NULL;
    }
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    lv_obj_t *top_content = lv_obj_create(root);
    if (top_content == NULL) {
        DIAG_FAIL("top content column");
        return NULL;
    }
    lv_obj_remove_flag(top_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(top_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_content, 0, 0);
    lv_obj_set_style_pad_all(top_content, 0, 0);
    lv_obj_set_style_pad_left(top_content, THEME_SPACE_L, 0);
    lv_obj_set_style_pad_right(top_content, THEME_SPACE_L, 0);
    lv_obj_set_style_pad_top(top_content, THEME_SPACE_L, 0);
    lv_obj_set_style_pad_row(top_content, THEME_SPACE_M, 0);
    lv_obj_set_flex_flow(top_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(top_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_width(top_content, lv_pct(100));
    lv_obj_set_height(top_content, LV_SIZE_CONTENT);

    w_info_row_cfg_t header_cfg = { .label = "CONNECTION", .show_status_dot = true };
    if (w_info_row_create(top_content, &cc->header_pill, &header_cfg) == NULL) {
        DIAG_FAIL("header_pill");
        return NULL;
    }

    if (make_receiver_card(top_content, cc) == NULL) {
        return NULL;   /* DIAG_FAIL already logged by the helper itself */
    }

    lv_obj_t *caption = lv_label_create(top_content);
    if (caption == NULL) {
        DIAG_FAIL("how-we-talk-to-it caption");
        return NULL;
    }
    lv_obj_remove_flag(caption, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(caption, &ui_font_meta, 0);
    lv_obj_set_style_text_color(caption, THEME_COLOR_TEXT_TERTIARY, 0);
    lv_label_set_text(caption, "HOW WE TALK TO IT");

    if (make_transport_cards_row(top_content, cc) == NULL) {
        return NULL;
    }

    if (make_volume_cap_row(root, cc) == NULL) {
        return NULL;
    }

    return root;
}

static void connection_mount(screen_ctx_t *ctx) { (void)ctx; }
static void connection_unmount(screen_ctx_t *ctx) { (void)ctx; }
static void connection_destroy(screen_ctx_t *ctx) { (void)ctx; }

static void connection_update(screen_ctx_t *ctx, const void *vm)
{
    screen_connection_ctx_t *cc = (screen_connection_ctx_t *)ctx->user_ctx;
    const connection_vm_t       *u  = (const connection_vm_t *)vm;

    w_info_row_set_status(&cc->header_pill, u->link_label, u->link_tone);
    w_info_row_set_value(&cc->row_host, u->host);
    w_info_row_set_value(&cc->row_round_trip, u->round_trip_display);
    /* live = false on both: "which transport are we talking over" is not a
     * liveness claim about a source, so these keep the plain white
     * highlight rather than the Receiver screen's green one. */
    w_source_status_card_set_state(&cc->telnet_card, u->telnet_selected, false, "Port 23 - live");
    w_source_status_card_set_state(&cc->http_card, u->http_selected, false, "Fallback - standby");
    w_volume_cap_slider_set_value(&cc->volume_cap, u->volume_limit_half_db_raw, u->volume_limit_display);
}

screen_if_t screen_connection_get_interface(void)
{
    screen_if_t iface = {
        .create   = connection_create,
        .mount    = connection_mount,
        .update   = connection_update,
        .unmount  = connection_unmount,
        .destroy  = connection_destroy,
    };
    return iface;
}
