#include "screen_stub.h"

static lv_obj_t *stub_create(screen_ctx_t *ctx)
{
    const screen_stub_cfg_t *cfg = (const screen_stub_cfg_t *)ctx->user_ctx;

    lv_obj_t *root = lv_obj_create(ctx->parent);   /* NOT lv_obj_create(NULL) -- see
                                                       * screen_ctx_t.parent's own comment
                                                       * (ui_router.h): NULL creates an
                                                       * independent top-level screen, never
                                                       * actually attached to the pager */
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(cfg->bg_rgb888), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    /* Same real finding as screen_now_playing.c/ui_router.c: LVGL's default
     * theme gives this plain lv_obj_create() object a small default radius
     * unless cleared, which for a full-screen object leaves the true 4
     * corners uncovered by this background. */
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);

    lv_obj_t *label = lv_label_create(root);
    lv_label_set_text(label, cfg->label);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);

    return root;
}

static void stub_noop(screen_ctx_t *ctx) { (void)ctx; }
static void stub_update_noop(screen_ctx_t *ctx, const void *vm) { (void)ctx; (void)vm; }

screen_if_t screen_stub_get_interface(void)
{
    screen_if_t iface = {
        .create   = stub_create,
        .mount    = stub_noop,
        .update    = stub_update_noop,
        .unmount  = stub_noop,
        .destroy  = stub_noop,
    };
    return iface;
}
