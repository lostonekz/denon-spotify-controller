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
 * Modal presentation (volume sheet, pickers, etc.) is NOT built here at
 * all -- deferred to Phase 6 with its first real caller, per the same
 * plan section: an API with zero real callers is exactly the kind of
 * thing to get wrong.
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

#ifdef __cplusplus
}
#endif

#endif /* UI_ROUTER_H */
