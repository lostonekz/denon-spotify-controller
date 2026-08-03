/*
 * The Receiver screen's power control -- a full-width rounded card wrapping
 * w_power_glyph (kept, real hit area) with a plain label ("The receiver's
 * on"/"...off") and a decorative, non-interactive switch-look indicator.
 *
 * Only one real code path submits anything: w_power_glyph_toggle() (see its
 * own header comment). The card's own root has its own LV_EVENT_CLICKED
 * handler calling the same function directly -- a bigger, more forgiving tap
 * target than the glyph's own 56px hit area alone, matching the mockup's
 * whole-row-looks-tappable presentation. The switch-look track/knob pair is
 * NOT a real lv_switch and has no click handler of its own -- a second
 * independent control here would have no way to avoid fighting the glyph's
 * own toggle over the same power_on state, so it only ever mirrors
 * w_power_row_set_state()'s argument, never initiates anything itself.
 */
#ifndef W_POWER_ROW_H
#define W_POWER_ROW_H

#include "lvgl.h"

#include "intent_submit_types.h"
#include "view_model_tone.h"
#include "w_power_glyph.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    intent_submit_provider_t intent_submit;   /* the RAW/unwrapped provider -- passed straight
                                                    into the inner w_power_glyph_cfg_t */
} w_power_row_cfg_t;

typedef struct {
    lv_obj_t          *root;
    lv_obj_t          *label;
    lv_obj_t          *switch_track;
    lv_obj_t          *switch_knob;
    w_power_glyph_t   glyph;
} w_power_row_t;

lv_obj_t *w_power_row_create(lv_obj_t *parent, w_power_row_t *w, const w_power_row_cfg_t *cfg);

/* Drives the glyph and the switch-look track/knob color+position from the
 * same two real signals w_power_glyph_set_state() already takes, and the
 * label from the caller's own precomputed text (view_model_denon.h's
 * denon_vm_t.power_label -- "Connecting..."/"The receiver is on"/"...off").
 * The label is NOT derived from power_on here: power_on defaults false
 * before svc_denon's first real write (core_state's own zero-init), which
 * is a genuinely different state ("we don't know yet") from a real,
 * confirmed-off receiver -- only the pure, host-tested view model builder
 * can tell the two apart (denon_state_t.meta.version), so this widget takes
 * the resulting string as-is rather than re-deriving a possibly-wrong one
 * from power_on alone. */
void w_power_row_set_state(w_power_row_t *w, bool power_on, tone_t denon_tone, const char *power_label);

#ifdef __cplusplus
}
#endif

#endif /* W_POWER_ROW_H */
