#include "w_stroke_label.h"

#include "theme_colors.h"

/* Shared style for all 8 stroke copies, across every w_stroke_label instance --
 * see this file's own header comment for why (avoids repeating the device-
 * picker's real per-object local-style-allocation hang). text_font is NOT part
 * of this shared style (it varies per instance -- hero/title/meta all appear in
 * this project) -- each instance's 4 copies still get one local
 * lv_obj_set_style_text_font() call each, a small, bounded local-style
 * footprint, not the burst of ~5 properties per object that caused the real
 * hang. */
static lv_style_t s_stroke_style;
static bool          s_stroke_style_inited = false;

static void ensure_stroke_style_inited(void)
{
    if (s_stroke_style_inited) {
        return;
    }
    lv_style_init(&s_stroke_style);
    lv_style_set_text_color(&s_stroke_style, THEME_COLOR_TEXT_STROKE);
    s_stroke_style_inited = true;
}

lv_obj_t *w_stroke_label_create(lv_obj_t *parent, w_stroke_label_t *w, const w_stroke_label_cfg_t *cfg)
{
    ensure_stroke_style_inited();

    w->root = lv_obj_create(parent);
    if (w->root == NULL) {
        return NULL;   /* LVGL object-pool exhaustion -- propagate the failure the
                           same way any other widget-build failure does (see
                           w_art_card_create()'s own contract), rather than
                           dereferencing a NULL object below. A real, reproducible
                           real-hardware finding, not a defensive-only guard: see
                           ui_app.c's UI_APP_LVGL_POOL_BYTES comment. */
    }
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    /* Real bug, found by direct review + confirmed against LVGL's own render code
     * (lv_refr.c: without this flag, child draw calls are clipped to this
     * object's own tight bounding box, obj->coords) -- the stroke copies below
     * are deliberately positioned OUTSIDE that box (negative dx/dy, or dx/dy
     * that push a copy past cfg->width/content height), so without this flag
     * every copy's overflowing edge was silently clipped, producing an
     * asymmetric, per-direction-broken stroke on real hardware (not a
     * simulator artifact -- this affected the already-shipped 4-direction
     * version too, just less visibly at a 1px offset than at the later 4px
     * diagnostic value). */
    lv_obj_add_flag(w->root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    /* Real bug, found live, and systemic across this whole screen: every
     * plain lv_obj_create() object is CLICKABLE by default (lv_obj.c:
     * obj->flags = LV_OBJ_FLAG_CLICKABLE at creation) -- this widget is
     * pure text display, never meant to be tappable itself, but left on,
     * every instance (device name, artist, VOL label, volume readout...)
     * silently sat in the touch hierarchy ready to swallow a tap meant for
     * whatever real control it happens to be positioned near (confirmed
     * contributing to the power glyph's own "does nothing" report, and a
     * likely factor near the volume slider's VOL/readout labels too).
     * Removed on every object this widget creates, below. */
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);
    lv_obj_set_size(w->root, cfg->width, LV_SIZE_CONTENT);

    /* 8-direction unit vectors (N/S/E/W/NE/NW/SE/SW), scaled by cfg->offset_px
     * at runtime -- offset_px varies per instance (1px mono/meta labels, 2px
     * Archivo body/hero text per the design system's own two real values), so
     * this can't be a single static const dx/dy table the way the old
     * 4-direction version was. */
    static const int32_t unit_dx[W_STROKE_LABEL_COPY_COUNT] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    static const int32_t unit_dy[W_STROKE_LABEL_COPY_COUNT] = { -1, 1, 0, 0, -1, -1, 1, 1 };

    for (int i = 0; i < W_STROKE_LABEL_COPY_COUNT; i++) {
        lv_obj_t *l = lv_label_create(w->root);
        if (l == NULL) {
            return NULL;
        }
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_style(l, &s_stroke_style, 0);
        lv_obj_set_style_text_font(l, cfg->font, 0);
        lv_obj_set_style_text_align(l, cfg->text_align, 0);
        lv_obj_set_width(l, cfg->width);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, unit_dx[i] * cfg->offset_px, unit_dy[i] * cfg->offset_px);
        w->stroke[i] = l;
    }

    w->fg = lv_label_create(w->root);
    if (w->fg == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->fg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(w->fg, cfg->font, 0);
    lv_obj_set_style_text_color(w->fg, cfg->color, 0);
    lv_obj_set_style_text_align(w->fg, cfg->text_align, 0);
    lv_obj_set_width(w->fg, cfg->width);
    lv_obj_align(w->fg, LV_ALIGN_TOP_LEFT, 0, 0);

    return w->root;
}

void w_stroke_label_set_text(w_stroke_label_t *w, const char *text)
{
    for (int i = 0; i < W_STROKE_LABEL_COPY_COUNT; i++) {
        lv_label_set_text(w->stroke[i], text);
    }
    lv_label_set_text(w->fg, text);
}

void w_stroke_label_set_color(w_stroke_label_t *w, lv_color_t color)
{
    lv_obj_set_style_text_color(w->fg, color, 0);
}
