#include "ui_router.h"

#include "theme_colors.h"

static void update_page_edge_tabs(ui_router_t *router)
{
    w_page_edge_tab_set_visible(&router->left_tab, router->active > 0);
    w_page_edge_tab_set_visible(&router->right_tab, router->active < UI_SCREEN_COUNT - 1);
}

static void dispatch_screen_change(ui_router_t *router, ui_screen_id_t new_active)
{
    if (router->registered[router->active] && router->screens[router->active].unmount != NULL) {
        router->screens[router->active].unmount(&router->ctxs[router->active]);
    }
    router->active = new_active;
    if (router->registered[new_active] && router->screens[new_active].mount != NULL) {
        router->screens[new_active].mount(&router->ctxs[new_active]);
    }
    update_page_edge_tabs(router);
}

static void on_left_tab_activate(void *ctx)
{
    ui_router_t *router = (ui_router_t *)ctx;
    if (router->active > 0) {
        ui_router_go_to_screen(router, (ui_screen_id_t)(router->active - 1));
    }
}

static void on_right_tab_activate(void *ctx)
{
    ui_router_t *router = (ui_router_t *)ctx;
    if (router->active < UI_SCREEN_COUNT - 1) {
        ui_router_go_to_screen(router, (ui_screen_id_t)(router->active + 1));
    }
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
    /* Real bug, found chasing a live "moving less than we should to the
     * right -- we see a little edge of the previous screen" report: pad_all
     * above does NOT cover flex gap -- lv_theme_default.c's own "scr" style
     * (applied to every LVGL screen, confirmed directly, and router->pager
     * IS a screen per lv_display_get_screen_active()) separately sets
     * pad_row/pad_column to PAD_SMALL (~10-14px, DPI-scaled), a real,
     * nonzero gap the default theme inserts between every flex child. Every
     * screen after the first was therefore sitting a few pixels further
     * right than id * page_w (this file's own scroll-target math) assumed,
     * so scrolling "to" a screen landed short of its true position, leaving
     * a sliver of the previous screen visible at the trailing edge. Same
     * class of bug as the bg_color/radius fix above -- another default-
     * theme property nothing here had explicitly zeroed yet. */
    lv_obj_set_style_pad_row(router->pager, 0, 0);
    lv_obj_set_style_pad_column(router->pager, 0, 0);
    lv_obj_set_flex_flow(router->pager, LV_FLEX_FLOW_ROW);
    /* Real bug, found chasing a persistent "swipe is still there" real-
     * hardware report that survived two earlier rounds of fixes (removing
     * every gesture handler in the codebase, then clearing SCROLLABLE from
     * every leaf object touch could land on -- w_art_card's image, the
     * page-edge tab's chevron). Neither round could have worked alone: read
     * directly, lv_indev_find_scroll_obj() (lv_indev_scroll.c) walks from
     * the PRESSED object up through *every* ancestor looking for the first
     * one that's still scrollable ("Check the pressed object and all of its
     * ancestors...") -- so as long as ANY ancestor in the chain stayed
     * scrollable, a drag anywhere on screen would still be claimed by it,
     * regardless of how many leaf objects got cleared below it. This
     * object -- router->pager itself, the actual top-level LVGL screen and
     * root ancestor of every screen/tab in the tree -- is exactly that
     * ancestor: the line below this comment used to just be a claim
     * ("deliberately NOT scrollable") with no real lv_obj_remove_flag()
     * call backing it up anywhere in this function. LVGL's own default
     * screen-creation path sets LV_OBJ_FLAG_SCROLLABLE the same as any
     * plain lv_obj_create() (screens are still plain lv_obj_t instances,
     * confirmed against lv_obj_class_create_obj() -- nothing about being a
     * *screen* clears it specially), so the pager was a fully native,
     * freely-draggable scroll container this entire time -- the real
     * source of "swipe is still there", and very likely also of "buttons
     * inconsistent" (a mid-drag pager position fighting the id-based tab/
     * scroll-to-x math elsewhere in this file). No scrollbar-mode call is
     * needed alongside this fix -- LV_OBJ_FLAG_SCROLLABLE gates LVGL's own
     * automatic scrollbar drawing too, confirmed by reading lv_obj_draw.c's
     * scrollbar draw path, so clearing this flag hides it as a side effect,
     * not a separate thing to remember. Programmatic scroll positioning
     * (ui_router_go_to_screen()'s own lv_obj_scroll_to_x() calls) does NOT
     * depend on this flag at all -- confirmed by reading lv_obj_scroll.c's
     * own bounds/position functions directly, none of which check it; it
     * only gates indev-driven drag-scroll initiation (lv_indev_scroll.c)
     * -- so removing it here breaks nothing about how this file already
     * moves between screens itself. No LV_EVENT_SCROLL_END handler either
     * -- that event is tied to an animation's own deletion (lv_obj_
     * scroll.c's scroll_end_cb, registered only on the animated path) and
     * never fires for an instant, non-animated scroll; ui_router_go_to_
     * screen() dispatches the screen change synchronously itself instead
     * of waiting on an event that a jump-cut would never send. */
    lv_obj_remove_flag(router->pager, LV_OBJ_FLAG_SCROLLABLE);

    w_page_edge_tab_cfg_t left_cfg = { .direction = LV_DIR_LEFT, .on_activate = on_left_tab_activate, .ctx = router };
    if (w_page_edge_tab_create(router->pager, &router->left_tab, &left_cfg) == NULL) {
        return false;
    }

    w_page_edge_tab_cfg_t right_cfg = { .direction = LV_DIR_RIGHT, .on_activate = on_right_tab_activate, .ctx = router };
    if (w_page_edge_tab_create(router->pager, &router->right_tab, &right_cfg) == NULL) {
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
     * to right by id -- UI_SCREEN_DENON(0)/NOW_PLAYING(1)/SETTINGS(2), the
     * design system's own fixed Receiver<-Now Playing->Browse order.
     * lv_obj_move_to_index() positions by id directly, so registration
     * CALL order doesn't matter, only the id values do. */
    lv_obj_set_size(root, lv_display_get_horizontal_resolution(router->disp),
                    lv_display_get_vertical_resolution(router->disp));
    lv_obj_move_to_index(root, (int32_t)id);
    /* Real bug, found chasing a live report of "buttons very inconsistent
     * across screens" + "Now Playing isn't landing centered on boot": the
     * comment above ("registration call order doesn't matter") was false as
     * implemented. left_tab/right_tab are ALSO children of router->pager
     * (created earlier, in ui_router_create()), so by the time this runs
     * they already occupy two of the pager's raw child-array slots --
     * lv_obj_move_to_index(root, id) targets that RAW array index, not a
     * position among screens only, so the two tabs' leftover slots offset
     * every id-based placement that comes after them. Confirmed via
     * ui_app.c's real call order (Now Playing registered FIRST, before
     * Denon/Settings, not in ascending id order): tracing
     * lv_obj_move_to_index()'s own array-shift logic (lv_obj_tree.c) by hand
     * for that exact call sequence lands Denon at flex slot 0 (correct by
     * coincidence), but Settings at slot 1 and Now Playing at slot 2 --
     * swapped. The boot-time lv_obj_scroll_to_x(..., UI_SCREEN_NOW_PLAYING *
     * page_w, ...) call below therefore scrolled to slot 1's physical
     * position, landing on the Settings/Browse stub instead of Now Playing,
     * and every id-based ui_router_go_to_screen() target was off by the same
     * amount -- exactly the reported symptoms. Fix: re-park both tabs to the
     * tail of the array (lv_obj_move_to_index(tab, -1), "last index") after
     * every screen registration. LVGL's flex layout already skips FLOATING
     * children (the tabs) when assigning row slots, so their own raw array
     * position never affects layout -- parking them at the tail just keeps
     * them out of the low indices id-based placement actually depends on,
     * restoring the "call order doesn't matter" contract the comment above
     * always intended, for real, regardless of registration order. */
    lv_obj_move_to_index(router->left_tab.root, -1);
    lv_obj_move_to_index(router->right_tab.root, -1);

    router->ctxs[id].root = root;
    router->registered[id] = true;

    if (id == UI_SCREEN_NOW_PLAYING && screen_if.mount != NULL) {
        screen_if.mount(&router->ctxs[id]);
    }

    /* Real bug, found live: "default screen was not correct" -- this used
     * to fire the boot-time scroll-to-Now-Playing call inline, right here,
     * gated on `id == UI_SCREEN_NOW_PLAYING`. ui_app.c registers Now
     * Playing FIRST, before Denon/Settings -- so at the exact moment that
     * scroll call ran, the pager held only Now Playing's own 480px of
     * content (plus the two zero-width-for-layout floating tabs), and
     * lv_obj_scroll_to_x() clamps its target to the pager's real scrollable
     * range, confirmed by reading lv_obj_scroll.c directly -- with only one
     * screen's worth of content in a same-width viewport, that range is
     * [0,0], so the requested "scroll to Now Playing's slot" silently
     * collapsed to x=0 and never moved again once Denon/Settings registered
     * afterward and actually filled out the layout. Whichever screen ended
     * up at flex slot 0 (Denon, per the id-ordering fix above) is what
     * actually showed on boot instead. Fix: defer the scroll (and the tab-
     * visibility update it feeds) until every screen is registered and the
     * pager's real, final content width exists -- mount() itself stays
     * tied to Now Playing's own registration above, since it only sets up
     * that screen's own subscriptions and doesn't depend on pager layout. */
    bool all_registered = true;
    for (int i = 0; i < UI_SCREEN_COUNT; i++) {
        if (!router->registered[i]) {
            all_registered = false;
            break;
        }
    }
    if (all_registered) {
        lv_obj_scroll_to_x(router->pager, (int32_t)UI_SCREEN_NOW_PLAYING * lv_display_get_horizontal_resolution(router->disp), LV_ANIM_OFF);
        update_page_edge_tabs(router);
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

bool ui_router_go_to_screen(ui_router_t *router, ui_screen_id_t target)
{
    if (target >= UI_SCREEN_COUNT || target == router->active) {
        return false;
    }
    /* Instant jump, not a slide -- real hardware feedback: even the
     * fixed-180ms triggered animation (a bounded, one-shot tween, not the
     * old continuous free-drag) looked choppy on this board's real
     * LV_DISPLAY_RENDER_MODE_PARTIAL + single small draw buffer, exactly
     * the kind of per-frame redraw cost this file's own header comment
     * already flagged as a real risk to verify, not assume away. Simply
     * draw the destination screen -- LV_ANIM_OFF makes lv_obj_scroll_to_x()
     * apply the new position in one step, with no intermediate frames to
     * look choppy. Dispatched synchronously right here, not via
     * LV_EVENT_SCROLL_END (which only ever fires for the animated path,
     * confirmed by reading lv_obj_scroll.c -- an instant scroll sends no
     * such event at all). */
    int32_t page_w = lv_obj_get_width(router->pager);
    lv_obj_scroll_to_x(router->pager, (int32_t)target * page_w, LV_ANIM_OFF);
    dispatch_screen_change(router, target);
    return true;
}
