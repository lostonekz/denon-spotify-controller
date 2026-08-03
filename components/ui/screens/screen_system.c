#include "screen_system.h"

#include "esp_log.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"
#include "view_model_system.h"

static const char *TAG = "screen_system";
#define DIAG_FAIL(step) ESP_LOGE(TAG, "%s returned NULL -- LVGL object allocation failed", step)

static lv_obj_t *system_create(screen_ctx_t *ctx)
{
    screen_system_ctx_t *sc = (screen_system_ctx_t *)ctx->user_ctx;

    lv_obj_t *root = lv_obj_create(ctx->parent);
    if (root == NULL) {
        DIAG_FAIL("system_create root");
        return NULL;
    }
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, THEME_SPACE_L, 0);
    lv_obj_set_style_pad_row(root, THEME_SPACE_M, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    sc->verdict_label = lv_label_create(root);
    if (sc->verdict_label == NULL) {
        DIAG_FAIL("verdict_label");
        return NULL;
    }
    lv_obj_remove_flag(sc->verdict_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(sc->verdict_label, &ui_font_title, 0);
    lv_obj_set_style_text_color(sc->verdict_label, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_width(sc->verdict_label, lv_pct(100));

    w_info_row_cfg_t wifi_cfg = { .label = "Wi-Fi", .show_status_dot = true };
    if (w_info_row_create(root, &sc->row_wifi, &wifi_cfg) == NULL) {
        DIAG_FAIL("row_wifi");
        return NULL;
    }

    w_info_row_cfg_t receiver_cfg = { .label = "Receiver", .show_status_dot = true };
    if (w_info_row_create(root, &sc->row_receiver, &receiver_cfg) == NULL) {
        DIAG_FAIL("row_receiver");
        return NULL;
    }

    w_info_row_cfg_t spotify_cfg = { .label = "Spotify", .show_status_dot = true };
    if (w_info_row_create(root, &sc->row_spotify, &spotify_cfg) == NULL) {
        DIAG_FAIL("row_spotify");
        return NULL;
    }

    w_info_row_cfg_t firmware_cfg = { .label = "Firmware", .show_status_dot = false };
    if (w_info_row_create(root, &sc->row_firmware, &firmware_cfg) == NULL) {
        DIAG_FAIL("row_firmware");
        return NULL;
    }

    w_info_row_cfg_t uptime_cfg = { .label = "Awake for", .show_status_dot = false };
    if (w_info_row_create(root, &sc->row_uptime, &uptime_cfg) == NULL) {
        DIAG_FAIL("row_uptime");
        return NULL;
    }

    w_info_row_cfg_t panel_cfg = { .label = "Panel", .show_status_dot = false };
    if (w_info_row_create(root, &sc->row_panel, &panel_cfg) == NULL) {
        DIAG_FAIL("row_panel");
        return NULL;
    }

    return root;
}

static void system_mount(screen_ctx_t *ctx) { (void)ctx; }
static void system_unmount(screen_ctx_t *ctx) { (void)ctx; }
static void system_destroy(screen_ctx_t *ctx) { (void)ctx; }

static void system_update(screen_ctx_t *ctx, const void *vm)
{
    screen_system_ctx_t *sc = (screen_system_ctx_t *)ctx->user_ctx;
    const system_vm_t       *u  = (const system_vm_t *)vm;

    lv_label_set_text(sc->verdict_label, u->verdict);
    w_info_row_set_status(&sc->row_wifi, u->wifi_display, u->wifi_tone);
    w_info_row_set_status(&sc->row_receiver, u->denon_display, u->denon_tone);
    w_info_row_set_status(&sc->row_spotify, u->spotify_display, u->spotify_tone);
    w_info_row_set_value(&sc->row_firmware, u->firmware_display);
    w_info_row_set_value(&sc->row_uptime, u->uptime_display);
    w_info_row_set_value(&sc->row_panel, u->panel_id);
}

screen_if_t screen_system_get_interface(void)
{
    screen_if_t iface = {
        .create   = system_create,
        .mount    = system_mount,
        .update   = system_update,
        .unmount  = system_unmount,
        .destroy  = system_destroy,
    };
    return iface;
}
