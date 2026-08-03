/*
 * A label with a black outline baked from 4 offset black copies behind the
 * real, colored foreground label -- LVGL 9's `lv_label` has no native
 * per-glyph stroke/outline style property (confirmed directly: no such
 * LV_STYLE_TEXT_* property exists), and this project's own font tool
 * (`lv_font_conv`, tools/gen_assets.py) has no outline-baking option
 * either (confirmed via its own --help). This is the standard LVGL
 * workaround for "caption-style" text that must stay legible over
 * unpredictable content (real album art, any brightness/color) without
 * depending on a solid backing (the scrim only covers the bottom band of
 * the art card, not the top-corner overlay labels).
 *
 * Real cost, deliberately bounded: 4 copies + 1 real label = 5 objects
 * per label instead of 1, using ONE shared `lv_style_t` for the black
 * copies (applied by reference, not 4 individual local style calls per
 * instance) -- this project already hit a real hardware hang once this
 * phase from a burst of individual per-object local style allocations
 * (the device-picker widget's own row buttons, since reverted); this
 * widget deliberately avoids repeating that mistake.
 */
#ifndef W_STROKE_LABEL_H
#define W_STROKE_LABEL_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define W_STROKE_LABEL_COPY_COUNT 8   /* 8-direction (N/S/E/W/NE/NW/SE/SW), per the
                                          design system's own explicit spec for this
                                          fallback technique ("eight-direction 1px
                                          black text-shadow") -- used uniformly for
                                          every instance, not just the mono/label
                                          class it was originally specified for: 8
                                          directions gives visibly smoother, more
                                          even coverage than 4 at any offset, so
                                          there's no reason to keep two copy-count
                                          tiers around */

typedef struct {
    const lv_font_t *font;
    lv_color_t        color;   /* foreground text color -- the stroke copies are
                                   always THEME_COLOR_TEXT_STROKE, not configurable */
    int32_t              width;   /* LV_SIZE_CONTENT for auto-width (matches this
                                      widget's own un-stroked predecessor label's
                                      behavior), or a fixed/LV_PCT value */
    lv_text_align_t          text_align;   /* LV_TEXT_ALIGN_AUTO (0, the zero-value
                                                default -- a caller that never sets
                                                this field gets exactly the same
                                                behavior as never calling
                                                lv_obj_set_style_text_align() at all) */
    int32_t              offset_px;   /* Stroke thickness in px. Required, no
                                          implicit default -- every caller sets this
                                          explicitly per the design system's own two
                                          real values: 1px for mono/meta-class labels
                                          (device name, VOL label, volume readout),
                                          2px for Archivo body/hero text (title,
                                          artist -- the spec's own "2px
                                          -webkit-text-stroke... same white and
                                          stroke" wording covers both). */
} w_stroke_label_cfg_t;

typedef struct {
    lv_obj_t *root;    /* transparent container -- what callers position/align,
                           matching every other widget's own w->root convention */
    lv_obj_t *stroke[W_STROKE_LABEL_COPY_COUNT];
    lv_obj_t *fg;
} w_stroke_label_t;

lv_obj_t *w_stroke_label_create(lv_obj_t *parent, w_stroke_label_t *w, const w_stroke_label_cfg_t *cfg);

/* Updates all 5 labels together -- the caller never touches .stroke[]/.fg directly. */
void w_stroke_label_set_text(w_stroke_label_t *w, const char *text);

/* Foreground color only -- the stroke stays black regardless. Only
 * volume_label needs this today (its color reflects TONE_LIVE vs not). */
void w_stroke_label_set_color(w_stroke_label_t *w, lv_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* W_STROKE_LABEL_H */
