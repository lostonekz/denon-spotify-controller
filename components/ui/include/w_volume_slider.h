/*
 * The real, live-dragging volume control -- Art-forward redesign's volume
 * row (VOL label -- track -- numeral readout). Wired to adr/0007's "one
 * true volume control" path: submits INTENT_DOMAIN_MULTI/
 * MULTI_INTENT_SET_VOLUME (never INTENT_DOMAIN_DENON directly -- that would
 * bypass main/app_wiring_volume.c's Spotify-retarget resolver), and applies
 * no client-side clamp (the safety clamp is enforced downstream in
 * svc_denon at the protocol boundary, by design, regardless of caller).
 *
 * Built on lv_slider_create() -- first use of that widget class in this
 * codebase, but one of LVGL's most standard/stable ones. Track radius is 0
 * (the user's own locked "square off the tracks too" answer, overriding
 * the design spec's own 7px value); knob is hidden entirely ("drag
 * anywhere along it," no knob, per spec). The 14px visual track sits
 * inside a 56px hit band via lv_obj_set_ext_click_area() -- the exact
 * mechanism lv_slider already uses internally at a smaller default, not a
 * new invention.
 *
 * Uses the RAW/unwrapped intent_submit_provider_t (screen_now_playing_ctx_t.
 * intent_submit_raw), not the pending-tracking wrapped one -- confirmed real
 * bug this would otherwise ship with: the wrapped provider's wrapped_submit()
 * sets a single screen-wide pending flag on every successful submit
 * regardless of domain, which dims/disables the transport row for the
 * whole drag if reused here. Denon volume feedback is a near-instant telnet
 * echo, unlike Spotify's poll-confirmed round trip that machinery exists
 * for -- this control genuinely doesn't need it.
 *
 * w_volume_slider_set_value() no-ops the track POSITION while the user is
 * actively dragging (LV_STATE_PRESSED) so the ~20ms telnet-echoed real
 * state doesn't fight the user's own finger -- but the readout label
 * itself updates live during a drag anyway, from the dragged value
 * directly (view_model_format_half_db()), not the lagging server value, so
 * the number the user sees while dragging is never stale.
 */
#ifndef W_VOLUME_SLIDER_H
#define W_VOLUME_SLIDER_H

#include <stdint.h>

#include "lvgl.h"

#include "intent_submit_types.h"
#include "w_stroke_label.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    intent_submit_provider_t intent_submit;   /* the RAW/unwrapped provider */
} w_volume_slider_cfg_t;

typedef struct {
    lv_obj_t         *row;
    lv_obj_t         *slider;
    w_stroke_label_t label_vol;        /* static "VOL" label */
    w_stroke_label_t label_readout;    /* live numeral, e.g. "52.5" */
    intent_submit_provider_t intent_submit;
} w_volume_slider_t;

lv_obj_t *w_volume_slider_create(lv_obj_t *parent, w_volume_slider_t *w, const w_volume_slider_cfg_t *cfg);

/* Called every screen update() tick with the real denon_state_t.volume_half_db
 * and its formatted display string (now_playing_vm_t.volume_display). */
void w_volume_slider_set_value(w_volume_slider_t *w, uint16_t volume_half_db, const char *display_text);

#ifdef __cplusplus
}
#endif

#endif /* W_VOLUME_SLIDER_H */
