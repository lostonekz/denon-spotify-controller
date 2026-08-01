/*
 * ui_router -- the 3-peer horizontal pager and screen lifecycle dispatch
 * (UI_ARCHITECTURE.md §3). Phase 5 Tier 4: the pager mechanics (swipe,
 * scroll-snap, page dots, create/mount/unmount lifecycle dispatch) are
 * real now; all three registered screens are trivial placeholders (a
 * label + background) until Tier 6 (Now Playing) and Phase 6 (Denon,
 * Settings) swap in real content -- deliberately built this way so the
 * mechanics are proven against something real before they're the only
 * thing standing between three real screens and a broken swipe (Phase 5
 * plan §8).
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

#include "w_page_dots.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCREEN_NOW_PLAYING = 0,
    UI_SCREEN_DENON,
    UI_SCREEN_SETTINGS,
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
    lv_obj_t       *pager;                      /* horizontal scroll-snap container,
                                                    * loaded as the active LVGL screen */
    w_page_dots_t    page_dots;                    /* Tier 6: real w_page_dots widget,
                                                       * same behavior as Tier 4's own
                                                       * inline dot row */

    screen_if_t       screens[UI_SCREEN_COUNT];
    screen_ctx_t       ctxs[UI_SCREEN_COUNT];
    bool                 registered[UI_SCREEN_COUNT];

    ui_screen_id_t     active;
} ui_router_t;

/* Creates the pager container (3 x panel-width, scroll-snap, horizontal)
 * and the page-dots indicator, and loads the pager as the active LVGL
 * screen. Call ui_router_register_screen() for each of the 3 ids after
 * this returns -- the router has nothing to show until at least one
 * screen is registered. */
bool ui_router_create(ui_router_t *router, lv_display_t *disp);

/* Calls screen_if.create() immediately (every Tier 4 screen is created at
 * registration time, never on demand -- see this file's own top comment
 * for why that's a deliberate Tier 4 simplification, not a permanent
 * constraint on every future screen) and mounts it if id == UI_SCREEN_NOW_
 * PLAYING (the default, active-on-boot screen per UI_ARCHITECTURE.md §3). */
bool ui_router_register_screen(ui_router_t *router, ui_screen_id_t id, screen_if_t screen_if, void *user_ctx);

/* Forwards to screen id's update() if it's registered. Safe to call for a
 * screen that isn't currently the visible/mounted one -- UI_ARCHITECTURE.md's
 * own "created once, never destroyed" screens are expected to stay
 * current even while off-screen, so swiping to them is instant. */
void ui_router_update(ui_router_t *router, ui_screen_id_t id, const void *vm);

#ifdef __cplusplus
}
#endif

#endif /* UI_ROUTER_H */
