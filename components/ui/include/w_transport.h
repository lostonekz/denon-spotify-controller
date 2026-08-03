/*
 * Prev / play-pause / next, with the pending-state treatment
 * (UI_ARCHITECTURE.md §4). No heart/favorite -- Phase 5 plan §6: cut
 * entirely, no Spotify endpoint or intent kind exists for it and it isn't
 * even in ROADMAP.md's Future Expansion list; shipping a permanently-dead
 * tap target is worse than not building it.
 *
 * Taps submit a real intent_t via the injected intent_submit_provider_t
 * (core_types/intent_submit_types.h) -- w_transport never touches
 * app_wiring/svc_spotify directly, same boundary every other ui/svc
 * crossing in this codebase respects.
 *
 * Art-forward redesign: symbol-font glyphs replaced with real drawn shapes
 * (design system's own custom-icon identity) -- prev/next are single white
 * triangles drawn via LVGL 9's real lv_draw_triangle() from a custom
 * LV_EVENT_DRAW_MAIN handler (the first low-level lv_draw_* call in this
 * codebase's `ui` layer; zero existing precedent to copy, verified against
 * managed_components/lvgl__lvgl/src/draw/lv_draw_triangle.h and a real
 * built-in widget's own custom-draw idiom, lv_switch.c, for the
 * obj->coords-is-absolute-screen-space convention draw points use). Kept
 * inside real 56px hit boxes per the "draw smaller than tappable" rule.
 * Play/pause is an 80px plain-lv_obj circle; its own inner triangle/two
 * bars are separate always-created children, visibility-toggled (no
 * per-tick allocation, matching this codebase's existing discipline), not
 * conditionally drawn inside one callback.
 *
 * Descoped, named not hidden: the design spec's "two 1.5px black
 * drop-shadows wrapping the transport group" isn't built -- LVGL's real
 * silhouette drop-shadow mechanism (drop_shadow_* style props) isn't
 * auto-wired for triangle draws (confirmed: only rect/label/image/line/arc
 * get it), and the prev/next hit boxes have no visible background for the
 * classic box-shadow style props to attach to either. Revisit only if real
 * hardware legibility against the (now scrim-less) artwork shows it's
 * actually needed.
 */
#ifndef W_TRANSPORT_H
#define W_TRANSPORT_H

#include <stdbool.h>

#include "lvgl.h"

#include "intent_submit_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    intent_submit_provider_t intent_submit;
    /* Denon domain -- power-on + input-select, submitted alongside
     * PLAYBACK_INTENT_RESUME_ON_AVR when Play is tapped with nothing
     * active anywhere (see on_play_pause_clicked()). Raw/unwrapped, same
     * reasoning as w_power_glyph/w_volume_slider's own intent_submit_raw:
     * a Denon action shouldn't dim/disable this row via the wrapped
     * provider's pending machinery, which exists for Spotify's poll-
     * confirmed round trip, not Denon's near-instant telnet echo. */
    intent_submit_provider_t intent_submit_raw;
} w_transport_cfg_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *btn_prev;
    lv_obj_t *btn_play_pause;
    lv_obj_t *play_triangle;    /* shown when paused (tap target: play) */
    lv_obj_t *pause_bar_l;
    lv_obj_t *pause_bar_r;      /* both shown when playing (tap target: pause) */
    lv_obj_t *btn_next;
    intent_submit_provider_t intent_submit;
    intent_submit_provider_t intent_submit_raw;
    bool                        is_playing;
    bool                        has_active_device;   /* now_playing_vm_t.has_active_device --
                                                        * see w_transport_set_state()'s own
                                                        * comment */
} w_transport_t;

lv_obj_t *w_transport_create(lv_obj_t *parent, w_transport_t *w, const w_transport_cfg_t *cfg);

/* is_playing drives which of play_triangle/pause_bar_{l,r} is visible,
 * always from the real state store's own is_playing (view_model_now_
 * playing_build() never overrides it optimistically -- a scoped Tier 6
 * decision: EVENTS_AND_STATE.md §6.3's "immediately renders the paused
 * state" is satisfied here by the pending dim/disable treatment below, a
 * real and visible response to the tap, without also building full
 * per-field optimistic-value overriding, which nothing else needs).
 * pending dims the whole row and makes it non-interactive -- that alone is
 * what changes immediately on tap; the glyph itself only changes once the
 * real state store agrees. has_active_device only changes which intent a
 * Play tap submits (on_play_pause_clicked()) -- it has no visual effect of
 * its own; the row doesn't grey out or change appearance just because
 * nothing is active yet. */
void w_transport_set_state(w_transport_t *w, bool is_playing, bool pending, bool has_active_device);

#ifdef __cplusplus
}
#endif

#endif /* W_TRANSPORT_H */
