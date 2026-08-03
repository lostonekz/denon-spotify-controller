#include "screen_denon.h"

#include "esp_log.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"
#include "view_model_denon.h"

/* Same null-check-and-propagate discipline as screen_now_playing.c's own
 * DIAG_FAIL -- a NULL return here used to crash instead of failing cleanly,
 * a real bug found on real hardware during the Art-forward redesign. */
static const char *TAG = "screen_denon";
#define DIAG_FAIL(step) ESP_LOGE(TAG, "%s returned NULL -- LVGL object allocation failed", step)

#define DENON_HEADER_TEXT "AVR-X3800H"   /* hardcoded -- no "friendly receiver name" setting
                                             exists anywhere in svc_settings; inventing one is
                                             out of scope for this screen */

static lv_obj_t *make_source_cards_row(lv_obj_t *parent, screen_denon_ctx_t *ctx)
{
    lv_obj_t *row = lv_obj_create(parent);
    if (row == NULL) {
        DIAG_FAIL("source cards row");
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

    w_source_status_card_cfg_t spotify_cfg = { .icon_kind = W_SOURCE_STATUS_CARD_ICON_BARS, .title = "Spotify" };
    if (w_source_status_card_create(row, &ctx->spotify_card, &spotify_cfg) == NULL) {
        DIAG_FAIL("spotify_card");
        return NULL;
    }

    w_source_status_card_cfg_t tv_cfg = { .icon_kind = W_SOURCE_STATUS_CARD_ICON_TV, .title = "TV audio" };
    if (w_source_status_card_create(row, &ctx->tv_card, &tv_cfg) == NULL) {
        DIAG_FAIL("tv_card");
        return NULL;
    }

    return row;
}

/* Mirrors screen_now_playing.c's own make_content_column()/volume-slider
 * placement exactly, so the volume control lands at the identical absolute
 * screen position/size on both screens (user request -- the control should
 * not visibly shift when paging between Now Playing and Receiver): root
 * itself carries NO padding (same as Now Playing's root), and the volume
 * slider gets its own bottom-anchored wrapper with THEME_SPACE_L (24px)
 * side padding and a THEME_SPACE_S (8px) bottom offset -- the exact same
 * two constants Now Playing's content column uses for its own left/right
 * padding and LV_ALIGN_BOTTOM_MID offset. Root's width (480px, shared by
 * both screens) makes this an exact match, not just a visual approximation. */
static lv_obj_t *make_volume_row(lv_obj_t *parent, screen_denon_ctx_t *dc)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    if (wrap == NULL) {
        DIAG_FAIL("volume row wrap");
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

    w_volume_slider_cfg_t volume_cfg = { .intent_submit = dc->intent_submit_raw };
    if (w_volume_slider_create(wrap, &dc->volume, &volume_cfg) == NULL) {
        DIAG_FAIL("volume_slider");
        return NULL;
    }
    lv_obj_set_width(dc->volume.row, lv_pct(100));

    return wrap;
}

static lv_obj_t *denon_create(screen_ctx_t *ctx)
{
    screen_denon_ctx_t *dc = (screen_denon_ctx_t *)ctx->user_ctx;

    lv_obj_t *root = lv_obj_create(ctx->parent);   /* NOT lv_obj_create(NULL) -- see
                                                         screen_ctx_t.parent's own comment */
    if (root == NULL) {
        DIAG_FAIL("denon_create root");
        return NULL;
    }
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root, 0, 0);   /* clear LVGL's default theme radius -- same real
                                                 corner-gap finding screen_now_playing.c's own
                                                 comment documents for a full-screen root */
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);   /* zero, matching Now Playing's root -- top_content
                                                 below carries its own THEME_SPACE_L padding
                                                 instead, so the volume wrap's own padding
                                                 (make_volume_row()) is the only thing
                                                 determining its position, same as Now Playing */

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
    lv_obj_set_style_pad_row(top_content, THEME_SPACE_L, 0);
    lv_obj_set_flex_flow(top_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(top_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_width(top_content, lv_pct(100));
    lv_obj_set_height(top_content, LV_SIZE_CONTENT);

    lv_obj_t *header = lv_label_create(top_content);
    if (header == NULL) {
        DIAG_FAIL("header label");
        return NULL;
    }
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(header, &ui_font_meta, 0);
    lv_obj_set_style_text_color(header, THEME_COLOR_TEXT_TERTIARY, 0);
    lv_label_set_text(header, DENON_HEADER_TEXT);

    w_power_row_cfg_t power_row_cfg = { .intent_submit = dc->intent_submit_raw };
    if (w_power_row_create(top_content, &dc->power_row, &power_row_cfg) == NULL) {
        DIAG_FAIL("power_row");
        return NULL;
    }
    lv_obj_set_width(dc->power_row.root, lv_pct(100));

    lv_obj_t *caption = lv_label_create(top_content);
    if (caption == NULL) {
        DIAG_FAIL("listening-to caption");
        return NULL;
    }
    lv_obj_remove_flag(caption, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(caption, &ui_font_meta, 0);
    lv_obj_set_style_text_color(caption, THEME_COLOR_TEXT_TERTIARY, 0);
    lv_label_set_text(caption, "LISTENING TO");

    if (make_source_cards_row(top_content, dc) == NULL) {
        return NULL;   /* DIAG_FAIL already logged by the helper itself */
    }

    if (make_volume_row(root, dc) == NULL) {
        return NULL;   /* DIAG_FAIL already logged by the helper itself */
    }

    return root;
}

static void denon_mount(screen_ctx_t *ctx) { (void)ctx; }
static void denon_unmount(screen_ctx_t *ctx) { (void)ctx; }
static void denon_destroy(screen_ctx_t *ctx) { (void)ctx; }

static void denon_update(screen_ctx_t *ctx, const void *vm)
{
    screen_denon_ctx_t *dc = (screen_denon_ctx_t *)ctx->user_ctx;
    const denon_vm_t       *u  = (const denon_vm_t *)vm;

    w_power_row_set_state(&dc->power_row, u->power_on, u->denon_tone, u->power_label);
    w_source_status_card_set_state(&dc->spotify_card, u->net_selected, u->net_subtitle);
    w_source_status_card_set_state(&dc->tv_card, u->tv_selected, "HDMI ARC");
    w_volume_slider_set_value(&dc->volume, u->volume_half_db_raw, u->volume_display);
}

screen_if_t screen_denon_get_interface(void)
{
    screen_if_t iface = {
        .create   = denon_create,
        .mount    = denon_mount,
        .update   = denon_update,
        .unmount  = denon_unmount,
        .destroy  = denon_destroy,
    };
    return iface;
}
