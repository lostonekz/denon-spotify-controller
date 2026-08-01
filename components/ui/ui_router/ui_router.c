#include "ui_router.h"

#include "theme_colors.h"

static void on_scroll_end(lv_event_t *e)
{
    ui_router_t *router = (ui_router_t *)lv_event_get_user_data(e);
    int32_t page_w = lv_obj_get_width(router->pager);
    int32_t x       = lv_obj_get_scroll_x(router->pager);
    int32_t page     = (page_w > 0) ? (int32_t)((x + page_w / 2) / page_w) : 0;
    if (page < 0) {
        page = 0;
    }
    if (page >= UI_SCREEN_COUNT) {
        page = UI_SCREEN_COUNT - 1;
    }
    ui_screen_id_t new_active = (ui_screen_id_t)page;
    if (new_active == router->active) {
        return;
    }

    if (router->registered[router->active] && router->screens[router->active].unmount != NULL) {
        router->screens[router->active].unmount(&router->ctxs[router->active]);
    }

    router->active = new_active;
    if (router->registered[new_active] && router->screens[new_active].mount != NULL) {
        router->screens[new_active].mount(&router->ctxs[new_active]);
    }
    w_page_dots_set_active(&router->page_dots, (uint8_t)new_active);
}

bool ui_router_create(ui_router_t *router, lv_display_t *disp)
{
    router->disp   = disp;
    router->pager = lv_display_get_screen_active(disp);
    /* A real finding from real-hardware testing (Phase 5 Tier 7): this
     * screen object is styled by LVGL's own default theme
     * (CONFIG_LV_USE_THEME_DEFAULT=y) same as any other lv_obj_create()
     * object -- confirmed by reading lv_theme_default.c directly, its
     * fallback "card" style gives every plain object a themed background
     * color AND a small default radius (RADIUS_DEFAULT, ~8-12px) unless
     * explicitly overridden. Nothing here previously set either, so the
     * pager showed the theme's own (light, not this app's dark palette)
     * default screen background -- and every registered screen's own root
     * (screen_now_playing.c, screen_stub.c) ALSO never zeroed its inherited
     * default radius, so their full-screen opaque backgrounds don't quite
     * reach the true 4 corners of the display, exposing the pager's
     * background underneath at exactly those corners. Explicit, correct,
     * radius-free background here is the real fix for the pager itself;
     * screen_now_playing.c/screen_stub.c fix their own roots the same way,
     * both belt and suspenders -- a future screen forgetting this should
     * still show this app's own dark color at its corners, never LVGL's
     * theme default. */
    lv_obj_set_style_bg_color(router->pager, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(router->pager, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(router->pager, 0, 0);
    lv_obj_set_style_border_width(router->pager, 0, 0);
    lv_obj_set_style_pad_all(router->pager, 0, 0);
    lv_obj_set_flex_flow(router->pager, LV_FLEX_FLOW_ROW);
    lv_obj_add_flag(router->pager, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(router->pager, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(router->pager, LV_SCROLL_SNAP_CENTER);
    /* A real finding from real-hardware testing (Phase 5 Tier 7), not a
     * style nicety: this was never set, so the pager used LVGL's own
     * default LV_SCROLLBAR_MODE_AUTO ("show whenever content overflows the
     * viewport") -- with 3 full-width pages, content is ALWAYS 3x the
     * viewport, so the default, unstyled scrollbar was permanently visible
     * as a plain white bar along the bottom edge, confirmed on a real photo
     * of the panel (sharp rectangular edges, not a lens/glass reflection).
     * w_page_dots already gives this exact "which page am I on" affordance
     * by design; the default scrollbar is both redundant and visually
     * wrong here. */
    lv_obj_set_scrollbar_mode(router->pager, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(router->pager, on_scroll_end, LV_EVENT_SCROLL_END, router);

    /* Page dots: a small overlay row, drawn on top of the pager rather
     * than inside it, so it doesn't scroll away with the active page. */
    w_page_dots_cfg_t dots_cfg = { .count = UI_SCREEN_COUNT };
    if (w_page_dots_create(router->pager, &router->page_dots, &dots_cfg) == NULL) {
        return false;
    }

    router->active = UI_SCREEN_NOW_PLAYING;
    for (int i = 0; i < UI_SCREEN_COUNT; i++) {
        router->registered[i] = false;
    }
    return true;
}

bool ui_router_register_screen(ui_router_t *router, ui_screen_id_t id, screen_if_t screen_if, void *user_ctx)
{
    if (id >= UI_SCREEN_COUNT || screen_if.create == NULL) {
        return false;
    }

    router->screens[id]           = screen_if;
    router->ctxs[id].root          = NULL;
    router->ctxs[id].parent        = router->pager;
    router->ctxs[id].user_ctx     = user_ctx;

    lv_obj_t *root = screen_if.create(&router->ctxs[id]);
    if (root == NULL) {
        return false;
    }
    /* Each screen is a page-width/height child of the pager, laid out left
     * to right in registration order -- Tier 4 registers NOW_PLAYING,
     * DENON, SETTINGS in exactly that order, matching UI_ARCHITECTURE.md
     * §3's left-to-right diagram. */
    lv_obj_set_size(root, lv_display_get_horizontal_resolution(router->disp),
                    lv_display_get_vertical_resolution(router->disp));
    lv_obj_move_to_index(root, (int32_t)id);

    router->ctxs[id].root = root;
    router->registered[id] = true;

    if (id == UI_SCREEN_NOW_PLAYING && screen_if.mount != NULL) {
        screen_if.mount(&router->ctxs[id]);
        w_page_dots_set_active(&router->page_dots, (uint8_t)id);
    }
    return true;
}

void ui_router_update(ui_router_t *router, ui_screen_id_t id, const void *vm)
{
    if (id >= UI_SCREEN_COUNT || !router->registered[id] || router->screens[id].update == NULL) {
        return;
    }
    router->screens[id].update(&router->ctxs[id], vm);
}
