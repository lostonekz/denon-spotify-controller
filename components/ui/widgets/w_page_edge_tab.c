#include "w_page_edge_tab.h"

#include "theme_colors.h"

#define W_PAGE_EDGE_TAB_HIT_W     64
#define W_PAGE_EDGE_TAB_HIT_H    112
#define W_PAGE_EDGE_TAB_VISUAL_W  30
#define W_PAGE_EDGE_TAB_VISUAL_H  88
#define W_PAGE_EDGE_TAB_RADIUS    11
#define W_PAGE_EDGE_TAB_CHEVRON   11   /* chevron bounding box, square */

/* Real bug, found live: true screen-vertical-center (the old dy=0) put this
 * 88px-tall visual roughly at y=196-284, which overlaps the Now Playing
 * title/artist block once that content column was tightened and pulled
 * toward the bottom (screen_now_playing.c's NOW_PLAYING_CONTENT_COLUMN_
 * HEIGHT). Fix: align to the transport row's own vertical center instead
 * of the screen's -- real empty space on both edges at that height, per
 * the design spec's own "Group centres at x = 240" note about the
 * transport row not needing the full screen width. This widget is owned
 * by ui_router (shared chrome across all 4 screens, not per-screen), so
 * the offset is a fixed pixel estimate computed from Now Playing's current
 * layout constants, not something derived live from any specific screen --
 * same tradeoff every other hand-computed pixel value in this codebase
 * makes, verified/adjusted against real hardware rather than measured
 * automatically: content stack spans y=196 to y=472 (276px tall, 8px
 * bottom margin); walking up from the bottom, volume (56px) + 8px gap puts
 * the transport row (80px) at y=328-408, center 368 -- 128px below true
 * screen-center (240).
 *
 * Recomputed from 123 when the scrub bar's drawn track went from 3px to
 * THEME_TRACK_H (14px), which grew the content column by 11px and pushed
 * the transport row 5px down. Because this constant is hand-derived from
 * another file's layout, it does NOT follow along on its own -- if Now
 * Playing's stack changes height again, this needs recomputing or the tab
 * silently drifts off the row it is supposed to align with. */
#define W_PAGE_EDGE_TAB_VERTICAL_OFFSET 128

static void fire(w_page_edge_tab_t *w)
{
    if (w->on_activate != NULL) {
        w->on_activate(w->ctx);
    }
}

static void on_clicked(lv_event_t *e)
{
    fire((w_page_edge_tab_t *)lv_event_get_user_data(e));
}

lv_obj_t *w_page_edge_tab_create(lv_obj_t *parent, w_page_edge_tab_t *w, const w_page_edge_tab_cfg_t *cfg)
{
    w->on_activate = cfg->on_activate;
    w->ctx           = cfg->ctx;

    bool is_right = (cfg->direction == LV_DIR_RIGHT);

    w->root = lv_obj_create(parent);
    if (w->root == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    /* Real bug, found live: router->pager (this object's parent) uses
     * LV_FLEX_FLOW_ROW to lay out the 3 screens -- without FLOATING, this
     * tab is treated as a 4th/5th flex item and POSITIONED BY THE FLEX
     * ALGORITHM (pushed off to the right of every screen, entirely outside
     * any visible viewport), silently overriding the lv_obj_align() call
     * below. FLOATING is LVGL's own real mechanism for "ignore the
     * parent's layout, position me via align/pos instead" -- confirmed,
     * lv_obj.h's own flag comment: "Do not scroll the object when the
     * parent scrolls and ignore layout". */
    lv_obj_add_flag(w->root, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);
    lv_obj_set_size(w->root, W_PAGE_EDGE_TAB_HIT_W, W_PAGE_EDGE_TAB_HIT_H);
    lv_obj_align(w->root, is_right ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, 0, W_PAGE_EDGE_TAB_VERTICAL_OFFSET);
    lv_obj_add_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(w->root, on_clicked, LV_EVENT_CLICKED, w);

    w->visual = lv_obj_create(w->root);
    if (w->visual == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->visual, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(w->visual, LV_OBJ_FLAG_CLICKABLE);   /* root owns the tap; the
                                                                 visual is purely decorative */
    lv_obj_set_style_radius(w->visual, W_PAGE_EDGE_TAB_RADIUS, 0);
    lv_obj_set_style_bg_color(w->visual, THEME_COLOR_TEXT_STROKE, 0);
    lv_obj_set_style_bg_opa(w->visual, 140, 0);   /* oklch(0.08 / 0.55) -- 0.55 * 255 ~= 140 */
    lv_obj_set_style_border_width(w->visual, 2, 0);
    lv_obj_set_style_border_color(w->visual, THEME_COLOR_TEXT_STROKE, 0);
    lv_obj_set_style_border_opa(w->visual, LV_OPA_COVER, 0);
    lv_obj_set_size(w->visual, W_PAGE_EDGE_TAB_VISUAL_W, W_PAGE_EDGE_TAB_VISUAL_H);
    /* dy=0, NOT W_PAGE_EDGE_TAB_VERTICAL_OFFSET -- real bug, found live: this
     * object's parent is w->root (lv_obj_create(w->root) above), not the
     * pager, so this align is already relative to root's own bounds. root
     * itself already carries the vertical offset (its own alignment is
     * relative to the pager); applying the same offset here a second time
     * double-shifted the visual ~246px down, past root's own 112px height
     * entirely -- invisible, while root's hit area (correctly offset once)
     * still worked fine. visual just needs to center within root, exactly
     * like before this offset existed. */
    lv_obj_align(w->visual, is_right ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, 0, 0);

    static const lv_point_precise_t chevron_right[3] = {
        { 0, 0 }, { W_PAGE_EDGE_TAB_CHEVRON / 2, W_PAGE_EDGE_TAB_CHEVRON / 2 }, { 0, W_PAGE_EDGE_TAB_CHEVRON },
    };
    static const lv_point_precise_t chevron_left[3] = {
        { W_PAGE_EDGE_TAB_CHEVRON / 2, 0 }, { 0, W_PAGE_EDGE_TAB_CHEVRON / 2 }, { W_PAGE_EDGE_TAB_CHEVRON / 2, W_PAGE_EDGE_TAB_CHEVRON },
    };
    w->chevron = lv_line_create(w->visual);
    if (w->chevron == NULL) {
        return NULL;
    }
    lv_line_set_points(w->chevron, is_right ? chevron_right : chevron_left, 3);
    lv_obj_remove_flag(w->chevron, LV_OBJ_FLAG_SCROLLABLE);   /* lv_line's own constructor
                                                                   clears CLICKABLE but not this
                                                                   -- same gap class as
                                                                   w_art_card.c's w->image */
    lv_obj_remove_flag(w->chevron, LV_OBJ_FLAG_CLICKABLE);   /* real bug, found live: every
                                                                  plain lv_obj (lv_line included)
                                                                  is CLICKABLE by default
                                                                  (lv_obj.c: obj->flags =
                                                                  LV_OBJ_FLAG_CLICKABLE at
                                                                  creation) -- left on, this
                                                                  child would swallow taps meant
                                                                  for w->root's own handler */
    lv_obj_set_style_line_width(w->chevron, 3, 0);
    lv_obj_set_style_line_rounded(w->chevron, true, 0);
    lv_obj_set_style_line_color(w->chevron, THEME_COLOR_TEXT_ONIMAGE, 0);
    lv_obj_center(w->chevron);

    return w->root;
}

void w_page_edge_tab_set_visible(w_page_edge_tab_t *w, bool visible)
{
    if (visible) {
        lv_obj_remove_flag(w->root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(w->root, LV_OBJ_FLAG_HIDDEN);
    }
}
