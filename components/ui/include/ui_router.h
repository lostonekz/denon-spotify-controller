/*
 * ui_router -- the 4-peer screen pager and lifecycle dispatch
 * (UI_ARCHITECTURE.md §3).
 *
 * Art-forward redesign: the free-drag scroll-snap pager (Phase 5 Tier 4's
 * original mechanism) is replaced entirely. Confirmed real, hardware-
 * measured reason: under this board's shipped LV_DISPLAY_RENDER_MODE_
 * PARTIAL + single small draw buffer (ui_app.c), a continuous drag-scroll
 * of two full-width screens redraws a large dirty rect on essentially
 * every touch-move tick (not once, like a fixed-duration animation) --
 * the most expensive redraw pattern available on this exact hardware
 * config, and the direct cause of the user's own "too slow" report. This
 * is now an instant jump-cut, tap-only, driven by ui_router_go_to_screen()
 * -- a fixed ~180ms slide (THEME_MOTION_STANDARD_MS) was tried first and
 * genuinely built, but real-hardware feedback found even that bounded,
 * one-shot animation choppy on this board's real render path, so it was
 * removed too: navigating now just draws the destination screen directly,
 * no animated frames at all. A swipe/gesture trigger was also built
 * alongside the tap and separately removed after real-hardware feedback
 * -- navigation goes through a tap on the side tabs alone now, nothing
 * else. w_page_dots is gone with it -- the page-edge tabs already
 * encode position and direction (which edge shows a tab, and which way it
 * points), so a separate "which of 3 pages am I on" indicator is
 * redundant clutter under the new quiet aesthetic.
 *
 * Screen order is fixed and never wraps: Receiver (UI_SCREEN_DENON,
 * leftmost) <- Now Playing (middle-left, default/boot screen) ->
 * Connection (UI_SCREEN_CONNECTION) -> System (UI_SCREEN_SYSTEM,
 * rightmost). Phase 6 addendum: the original "Browse"/UI_SCREEN_SETTINGS
 * placeholder is retired -- its real content turned out to be two
 * screens' worth (Denon connection/latency/transport info, and overall
 * panel health), not one, per the user's own split design. Denon/Now
 * Playing's relative order and the boot default are unchanged from when
 * this was written; only the two rightmost slots were replaced.
 *
 * Modal presentation: a single, minimal OVERLAY slot now exists, built with
 * its first real caller (the Spotify pair screen) rather than speculatively,
 * per the same plan section -- an API with zero real callers is exactly the
 * kind of thing to get wrong. It is deliberately one slot, not a stack:
 * nothing in this project needs a modal over a modal, and a depth-N stack
 * would be four more states to get wrong for no caller.
 *
 * The overlay is a child of the pager, NOT lv_display_get_layer_top(). This
 * is a real render-cost decision, not a stylistic one: lv_refr_get_top_obj()
 * searches for a fully-opaque covering object only WITHIN the active screen
 * (lv_refr.c calls it with lv_display_get_screen_active(disp)), and uses it
 * to skip drawing everything underneath. An opaque, full-size pager child
 * wins that search, so the four screens below are never traversed. Something
 * parked in layer_top cannot: lv_refr draws the top/sys layers IN ADDITION
 * to a full active-screen pass. On this board's LV_DISPLAY_RENDER_MODE_
 * PARTIAL + single 40-row draw buffer that is the difference between one
 * screen's worth of drawing and two.
 *
 * That culling depends on the overlay root staying genuinely opaque:
 * bg_opa COVER, radius 0, no transform, no scrim. Adding a translucent
 * scrim later would silently give the cost back.
 */
#ifndef UI_ROUTER_H
#define UI_ROUTER_H

#include <stdbool.h>

#include "lvgl.h"

#include "w_page_edge_tab.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCREEN_DENON = 0,      /* "Receiver" per the design system's own naming -- leftmost */
    UI_SCREEN_NOW_PLAYING,    /* default/boot screen */
    UI_SCREEN_CONNECTION,     /* Denon host/latency/transport + volume-cap slider */
    UI_SCREEN_SYSTEM,         /* overall panel health -- rightmost */
    UI_SCREEN_COUNT,
} ui_screen_id_t;

/* Handed to every screen_if_t callback. `root` is filled in by ui_router
 * right after create() returns -- create() itself receives ctx with root
 * still NULL, since building that root IS create()'s job. `user_ctx` is
 * whatever the registrant passed to ui_router_register_screen(); screens
 * cast it to their own real type. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *parent;     /* set by ui_router before create() runs -- the pager
                             * container create() must build its root against
                             * (lv_obj_create(ctx->parent), never
                             * lv_obj_create(NULL): NULL creates an independent
                             * top-level LVGL *screen*, not a pager child --
                             * confirmed directly against lv_obj_class_create_obj()
                             * -- and lv_obj_move_to_index() on a parentless
                             * object is a silent no-op (lv_obj_tree.c: "parent
                             * is NULL", logs a warning and returns). A real
                             * Tier 4 bug, found and fixed in Tier 6: every
                             * screen was silently never attached to the pager. */
    void      *user_ctx;
} screen_ctx_t;

typedef struct {
    lv_obj_t *(*create)   (screen_ctx_t *ctx);   /* build the tree */
    void      (*mount)    (screen_ctx_t *ctx);   /* subscribe, animate in */
    void      (*update)   (screen_ctx_t *ctx, const void *vm);
    void      (*unmount)  (screen_ctx_t *ctx);   /* unsubscribe, stop anims -- MUST
                                                    * stop every animation/timer it
                                                    * started (UI_ARCHITECTURE.md §3) */
    void      (*destroy)  (screen_ctx_t *ctx);
} screen_if_t;

typedef struct {
    lv_display_t     *disp;
    lv_obj_t       *pager;                      /* horizontal, non-scrollable container,
                                                    * loaded as the active LVGL screen --
                                                    * positioned only via
                                                    * ui_router_go_to_screen()'s own
                                                    * animation, never by touch-drag */
    w_page_edge_tab_t left_tab;                    /* Receiver-ward, hidden on the
                                                       leftmost screen */
    w_page_edge_tab_t right_tab;                   /* System-ward, hidden on the
                                                       rightmost screen */

    screen_if_t       screens[UI_SCREEN_COUNT];
    screen_ctx_t       ctxs[UI_SCREEN_COUNT];
    bool                 registered[UI_SCREEN_COUNT];

    ui_screen_id_t     active;

    screen_if_t       overlay;                     /* the single modal slot -- see the
                                                       file header for why it lives in the
                                                       pager and not layer_top */
    screen_ctx_t       overlay_ctx;
    bool                 overlay_registered;
    bool                 overlay_presented;
} ui_router_t;

/* Creates the pager container (4 x panel-width, horizontal, non-
 * scrollable) and both page-edge tabs, and loads the pager as the active
 * LVGL screen. Call ui_router_register_screen() for each of the 4 ids
 * after this returns -- the router has nothing to show until at least one
 * screen is registered. */
bool ui_router_create(ui_router_t *router, lv_display_t *disp);

/* Calls screen_if.create() immediately (every screen is created at
 * registration time, never on demand -- deliberate, matching this
 * router's own "created once, never destroyed" contract) and mounts it if
 * id == UI_SCREEN_NOW_PLAYING (the default, active-on-boot screen). */
bool ui_router_register_screen(ui_router_t *router, ui_screen_id_t id, screen_if_t screen_if, void *user_ctx);

/* Forwards to screen id's update() if it's registered. Safe to call for a
 * screen that isn't currently the visible/mounted one -- UI_ARCHITECTURE.md's
 * own "created once, never destroyed" screens are expected to stay
 * current even while off-screen, so paging to them is instant. */
void ui_router_update(ui_router_t *router, ui_screen_id_t id, const void *vm);

/* Jumps instantly from the currently active screen to `target` via LVGL's
 * own public lv_obj_scroll_to_x(pager, ..., LV_ANIM_OFF) -- no animation.
 * A fixed ~180ms slide (LV_ANIM_ON) was the first real implementation, but
 * real-hardware feedback found it choppy on this board's real render path
 * (LV_DISPLAY_RENDER_MODE_PARTIAL + a single small draw buffer, ui_app.c
 * -- even a bounded, one-shot tween still tiles several full-width redraw
 * passes through it per animated frame) and asked for it removed entirely:
 * this now just draws the destination screen directly, one step, no
 * intermediate frames to look choppy. Dispatches unmount()/mount()
 * synchronously, right here -- not via LV_EVENT_SCROLL_END, which is
 * tied to an animation's own deletion (lv_obj_scroll.c's scroll_end_cb)
 * and never fires for a non-animated scroll; there is no event to wait on
 * for an instant jump. Returns false (no-op) if target is out of range or
 * already active -- the fixed, non-wrapping order is enforced here, not
 * left to callers. Called by a w_page_edge_tab tap only -- navigation is
 * tap-only, deliberately (see w_page_edge_tab.h's own top comment);
 * nothing in this router listens for LV_EVENT_GESTURE. */
bool ui_router_go_to_screen(ui_router_t *router, ui_screen_id_t target);

/* Registers the single overlay slot. Like every screen here it is CREATED
 * IMMEDIATELY and never destroyed -- present/dismiss only toggle visibility.
 * That matters more than usual for this particular overlay: its QR canvas is
 * a ~7.7 KB draw buffer, and this board's history includes LVGL allocation
 * failures under UI-task memory pressure. Paying it at boot turns a possible
 * mid-session hang into a deterministic ui_app_create() failure.
 *
 * MUST be called AFTER all UI_SCREEN_COUNT screens are registered:
 * ui_router_register_screen() re-parks both page-edge tabs at child index -1
 * on every call, so an overlay registered earlier would be shuffled below
 * them and lose both its z-order and its input priority.
 *
 * Does NOT call mount() -- that fires on present(), which is the whole point
 * of a modal's lifecycle (and, for the pair screen, is what starts and stops
 * the pairing HTTP listener). */
bool ui_router_register_overlay(ui_router_t *router, screen_if_t screen_if, void *user_ctx);

/* Shows the overlay, hides both page-edge tabs, and dispatches its mount().
 * False if no overlay is registered or one is already up. */
bool ui_router_present_overlay(ui_router_t *router);

/* Dispatches unmount() FIRST, then hides the overlay and restores the tabs to
 * whatever the active screen says they should be. False if nothing is up. */
bool ui_router_dismiss_overlay(ui_router_t *router);

bool ui_router_overlay_is_presented(const ui_router_t *router);

/* Forwards to the overlay's update(), only while it is actually presented.
 * Deliberately a separate entry point rather than an extra id understood by
 * ui_router_update(): one function meaning two different things depending on
 * hidden state is how the screen-vs-tab index bugs in this file happened. */
void ui_router_update_overlay(ui_router_t *router, const void *vm);

#ifdef __cplusplus
}
#endif

#endif /* UI_ROUTER_H */
