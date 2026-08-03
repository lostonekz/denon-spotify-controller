/*
 * A page-edge navigation tab -- Art-forward redesign's replacement for the
 * old free-drag swipe pager (confirmed real, hardware-measured cost: a
 * full-width scroll-snap drag redraws ~12 tiled passes through the small
 * PARTIAL-mode draw buffer on every touch-move tick; a page-edge tap
 * instead triggers one bounded, fixed-duration transition -- see
 * ui_router.c's own ui_router_go_to_screen()).
 *
 * Owned by ui_router (two instances, left/right, built once as overlay
 * children of the pager -- the same "overlay on the pager, not per-screen"
 * idiom the old w_page_dots used), not by each screen. Visibility and
 * target are updated by ui_router on every transition from a small fixed
 * table: end screens (Receiver, Browse) show only the tab pointing back
 * toward Now Playing; the middle screen (Now Playing) shows both. Order is
 * fixed and never wraps.
 *
 * Navigation is tap-only, deliberately -- gesture/swipe-based page
 * navigation (the design system's own original "tap or swipe... over
 * 180ms" wording) was built, then explicitly removed after real-hardware
 * feedback: the user wants navigation through the side tabs alone, no
 * swipe trigger anywhere. Nothing in this widget or ui_router.c listens
 * for LV_EVENT_GESTURE or any scroll/drag gesture -- confirmed by grep,
 * not just by this comment.
 *
 * "Draw smaller than tappable": the 30x88 visual sits inside a 64x112 hit
 * column, both flush to the true screen edge -- the design system's own
 * rule, already applied elsewhere in this codebase via THEME_TOUCH_MIN.
 *
 * Deliberate simplification, named not hidden: the visual's rounded corners
 * are a uniform LV_RADIUS_CIRCLE-style radius on all 4 corners, not the
 * spec's literal asymmetric "11px 0 0 11px" (LVGL 9's rect draw descriptor
 * has a single uniform radius field -- no per-corner support -- confirmed
 * directly, components/ui has no existing precedent for an asymmetric-
 * corner composite either). The visual sits flush against the true screen
 * boundary, where the "wrong" two corners' curvature is barely visible in
 * practice; revisit with a corner-patch overlay only if real hardware shows
 * it matters.
 */
#ifndef W_PAGE_EDGE_TAB_H
#define W_PAGE_EDGE_TAB_H

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_dir_t direction;               /* LV_DIR_LEFT or LV_DIR_RIGHT -- which screen edge
                                          this tab sits on, and which chevron/gesture
                                          direction it responds to */
    void (*on_activate)(void *ctx);
    void    *ctx;
} w_page_edge_tab_cfg_t;

typedef struct {
    lv_obj_t *root;      /* 64x112 hit column, flush to the screen edge */
    lv_obj_t *visual;     /* 30x88 visible chip */
    lv_obj_t *chevron;    /* lv_line, a real standard LVGL widget -- no custom draw
                              needed for a 2-segment stroked chevron */
    void (*on_activate)(void *ctx);
    void    *ctx;
} w_page_edge_tab_t;

lv_obj_t *w_page_edge_tab_create(lv_obj_t *parent, w_page_edge_tab_t *w, const w_page_edge_tab_cfg_t *cfg);

void w_page_edge_tab_set_visible(w_page_edge_tab_t *w, bool visible);

#ifdef __cplusplus
}
#endif

#endif /* W_PAGE_EDGE_TAB_H */
