/*
 * The Connection screen's "Don't go past" volume-cap control -- near-
 * identical construction to w_volume_slider.c (square track, no knob, 56px
 * hit band, first-class lv_slider_create()), but amber-colored (matching
 * the mockup's own "amber, not green: it constrains rather than plays"
 * framing) and submitting INTENT_DOMAIN_SETTINGS/
 * SETTINGS_INTENT_SET_VOLUME_LIMIT instead of INTENT_DOMAIN_MULTI --
 * this changes SETTING_VOLUME_LIMIT_MAX itself (via main/app_wiring_
 * ticks.c's new dispatch case), not a live volume value.
 *
 * Plain lv_label_create() text, not w_stroke_label -- this screen's
 * background is a plain designed dark surface, never unpredictable album
 * art, same reasoning screen_denon.c already established for the Receiver
 * screen.
 *
 * Uses the RAW/unwrapped intent_submit_provider_t, same reasoning as every
 * other Denon-adjacent control on these screens: a settings change is a
 * single discrete action, not something that should engage Now Playing's
 * own pending/dim machinery (which this screen doesn't even have).
 */
#ifndef W_VOLUME_CAP_SLIDER_H
#define W_VOLUME_CAP_SLIDER_H

#include <stdint.h>

#include "lvgl.h"

#include "intent_submit_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    intent_submit_provider_t intent_submit;   /* the RAW/unwrapped provider */
} w_volume_cap_slider_cfg_t;

typedef struct {
    lv_obj_t *row;             /* a COLUMN: caption line, then the bar row */
    lv_obj_t *slider;
    lv_obj_t *label_caption;   /* static "Don't go past", on its own line above the bar */
    lv_obj_t *label_readout;   /* live numeral, e.g. "35.0" */
    intent_submit_provider_t intent_submit;
} w_volume_cap_slider_t;

lv_obj_t *w_volume_cap_slider_create(lv_obj_t *parent, w_volume_cap_slider_t *w,
                                        const w_volume_cap_slider_cfg_t *cfg);

/* Called every screen update() tick with denon_state_t.volume_limit_half_db
 * and its formatted display string. No-ops the slider position while the
 * user is actively dragging, same reasoning as w_volume_slider_set_value(). */
void w_volume_cap_slider_set_value(w_volume_cap_slider_t *w, uint16_t volume_limit_half_db,
                                       const char *display_text);

#ifdef __cplusplus
}
#endif

#endif /* W_VOLUME_CAP_SLIDER_H */
