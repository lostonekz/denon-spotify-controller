/*
 * The Now Playing header's power glyph -- Art-forward redesign. Currently
 * unused: removed from screen_now_playing.c live, once pressing Play was
 * confirmed to already wake the AVR and switch to the HEOS Music input
 * when nothing's playing (w_transport.c's PLAYBACK_INTENT_RESUME_ON_AVR
 * path), making a separate standing power control redundant. Kept, not
 * deleted -- real Phase 6 Receiver-screen infrastructure, same "kept for a
 * real planned future caller" precedent as w_status_pill.c.
 *
 * A real, functional power + source toggle (a deliberate exception to
 * "look and feel, not new functionality" -- the user asked for this exact
 * behavior directly):
 *
 *   tap while off -> power the receiver on AND switch to the HEOS Music
 *                    input (DENON_INPUT_NET)
 *   tap while on  -> switch to the TV Audio input (DENON_INPUT_TV) AND
 *                    power the receiver off
 *
 * Both DENON_INTENT_SET_POWER and DENON_INTENT_SET_INPUT are discrete
 * commands (denon_intent.c), submitted through a plain FIFO
 * (denon_cmdq_submit_discrete()) that's never reordered or coalesced -- one
 * tap submitting both back-to-back is correctly sequenced at the queue
 * level. What is NOT yet verified: whether the real AVR-X3800H accepts a
 * SET_INPUT command immediately after SET_POWER(on), or needs real settle
 * time -- flagged for real-hardware verification, not assumed.
 *
 * Uses the RAW/unwrapped intent_submit_provider_t (screen_now_playing_ctx_t.
 * intent_submit_raw), not the pending-tracking wrapped one -- a power/source
 * toggle is a single discrete action on a different domain than playback and
 * shouldn't fight the transport row's own pending/dim machinery (the same
 * reasoning w_volume_slider uses).
 *
 * No optimistic flip on tap -- the glyph waits for the real power_on/
 * denon_tone update (a fast, near-instant telnet echo) rather than guessing.
 *
 * Ring + notch need no custom LVGL draw code at all -- both are plain
 * lv_obj_create() rects/circles, the same "standard rect-draw path" idiom
 * w_status_pill.c/w_page_dots.c already use. The "outline against the art"
 * effect uses LVGL 9's real lv_obj_set_style_drop_shadow_* properties
 * (confirmed auto-wired into rect draw, no custom draw callback needed) --
 * one drop-shadow per shape, not the design spec's literal two stacked CSS
 * filters (LVGL exposes one per object/part), a named simplification.
 */
#ifndef W_POWER_GLYPH_H
#define W_POWER_GLYPH_H

#include <stdbool.h>

#include "lvgl.h"

#include "intent_submit_types.h"
#include "view_model_tone.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    intent_submit_provider_t intent_submit;   /* the RAW/unwrapped provider */
} w_power_glyph_cfg_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *ring;
    lv_obj_t *notch;
    intent_submit_provider_t intent_submit;
    bool                        last_power_on;   /* drives the tap handler's toggle target --
                                                       always the last real state pushed via
                                                       w_power_glyph_set_state(), never
                                                       optimistically written by the tap itself */
} w_power_glyph_t;

lv_obj_t *w_power_glyph_create(lv_obj_t *parent, w_power_glyph_t *w, const w_power_glyph_cfg_t *cfg);

/* Colors the ring/notch from the combination of both real signals (gray =
 * off regardless of link tone; green = on and confirmed-healthy; amber = on
 * but the Denon link is currently STALE/RECONNECTING/UNAVAILABLE) and
 * records power_on for the next tap's toggle target. */
void w_power_glyph_set_state(w_power_glyph_t *w, bool power_on, tone_t denon_tone);

#ifdef __cplusplus
}
#endif

#endif /* W_POWER_GLYPH_H */
