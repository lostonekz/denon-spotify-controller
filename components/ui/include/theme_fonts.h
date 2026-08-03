/*
 * extern declarations for tools/gen_assets.py's real, already-generated
 * output (the .c files under components/ui/assets/fonts/) -- each font file only defines
 * `const lv_font_t ui_font_<name>`, no header of its own; declared here
 * once so screens/widgets don't each need their own extern.
 */
#ifndef THEME_FONTS_H
#define THEME_FONTS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t ui_font_display;   /* 44 px / 700 */
extern const lv_font_t ui_font_hero;      /* 38 px / 700 -- Art-forward's on-image track title
                                              (resized from 34px for the Art-forward redesign) */
extern const lv_font_t ui_font_title;     /* 30 px / 600 */
extern const lv_font_t ui_font_body;      /* 20 px / 400 -- Art-forward's on-image artist line */
extern const lv_font_t ui_font_label;     /* 16 px / 500 */
extern const lv_font_t ui_font_meta;      /* 13 px / 500, mono */
extern const lv_font_t ui_font_meta_lg;   /* 15 px / 500, mono -- Art-forward's volume readout;
                                              the VOL label itself reuses ui_font_meta rather
                                              than baking a third near-duplicate mono size for
                                              the spec's own 11px value */

#ifdef __cplusplus
}
#endif

#endif /* THEME_FONTS_H */
