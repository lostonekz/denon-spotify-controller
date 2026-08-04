/*
 * Interpolated progress (UI_ARCHITECTURE.md §4), with real drag-to-seek --
 * added live, after real-hardware feedback explicitly asked for the
 * scrubber to actually navigate the track. Phase 4/5's original "no SEEK"
 * scope decision (no endpoint wired, no scrub UI) is gone: PLAYBACK_INTENT_
 * SEEK now exists end to end (playback_intent_types.h, spotify_intent.c,
 * spotify_api.c's `PUT .../seek?position_ms=`).
 *
 * Built on lv_slider_create(), same "drag anywhere, no visible knob" idiom
 * w_volume_slider.c already proved out -- not lv_bar, which has no touch
 * input of its own at all. Unlike volume, the seek intent is submitted
 * ONCE, on LV_EVENT_RELEASED, not on every LV_EVENT_VALUE_CHANGED during
 * the drag (confirmed by reading lv_slider.c directly: VALUE_CHANGED fires
 * continuously while dragging) -- Denon's cmdq coalesces/rate-limits
 * volume drags locally for free, but a real Spotify seek call has no such
 * protection, so firing one per touch-move sample would hammer the API.
 * w_progress_bar_set_progress()'s own bar-value update no-ops while
 * LV_STATE_PRESSED is set, same reasoning as w_volume_slider_set_value()'s
 * own guard -- otherwise the tick-driven real position would fight the
 * user's own finger mid-drag.
 *
 * Art-forward redesign: square track (0 radius, the user's own locked
 * "square off the tracks too" answer, overriding the spec's own 2px), and
 * a flat fill, not the old per-track dynamic accent colour
 * (now_playing_vm_t.accent_rgb565 is removed by this same redesign).
 *
 * That fill is THEME_COLOR_ACCENT_LIVE at full opacity -- the same green
 * w_volume_slider uses, not the spec's own "white elapsed fill at 34%".
 * The two tracks sit directly above each other on Now Playing and are now
 * both real drag controls, so they read as one family; the spec's white
 * was written for a bar meant only to be looked at. The drawn height
 * (THEME_TRACK_H) is shared with the volume tracks for the same reason.
 *
 * Elapsed/remaining timestamps: the design spec itself said "no timestamps
 * in this direction -- traded away for the art," but the user explicitly
 * asked for them back after seeing the redesign on real hardware -- restored
 * here as a below-the-bar row (w_stroke_label, same mono/1px-stroke
 * treatment as every other small overlay label), the bar's own square/flat
 * styling otherwise unchanged.
 *
 * The timestamps are drag-aware, not a passive readout: while the bar is
 * held they are derived from the DRAGGED position (LV_EVENT_VALUE_CHANGED),
 * so they answer "what time am I about to land on?" -- the one question a
 * scrubber exists to answer. They previously kept reporting live playback
 * throughout a drag, which made the bar draggable but not really scrubbable.
 * w_progress_bar_set_progress()'s own writes are guarded against
 * LV_STATE_PRESSED for the same reason its bar-position write already was.
 */
#ifndef W_PROGRESS_BAR_H
#define W_PROGRESS_BAR_H

#include <stdint.h>

#include "lvgl.h"

#include "intent_submit_types.h"
#include "w_stroke_label.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t width;
    /* Wrapped -- same reasoning as w_transport's own intent_submit: seek is
     * a genuine Spotify round trip (poll-confirmed, not near-instant like
     * Denon), so a real failure should surface through the existing
     * pending/REJECTED-ack/toast machinery like any other playback action. */
    intent_submit_provider_t intent_submit;
} w_progress_bar_cfg_t;

typedef struct {
    lv_obj_t         *root;   /* column: bar, then the elapsed/remaining row */
    lv_obj_t         *bar;    /* an lv_slider despite the field name -- see this
                                  header's own top comment */
    w_stroke_label_t elapsed_label;
    w_stroke_label_t remaining_label;
    intent_submit_provider_t intent_submit;
    uint32_t                     duration_ms;   /* stashed from the last set_progress() call --
                                                     on_released() needs the real track length to
                                                     convert a drag position back into an absolute
                                                     seek target */
} w_progress_bar_t;

lv_obj_t *w_progress_bar_create(lv_obj_t *parent, w_progress_bar_t *w, const w_progress_bar_cfg_t *cfg);

/* permille: 0..1000 (now_playing_vm_t.progress_permille). elapsed/
 * remaining: already-formatted strings (view_model_format_mmss()/
 * view_model_format_remaining_mmss()). duration_ms: now_playing_vm_t.
 * duration_ms, the raw source value the formatted strings were built
 * from -- needed here for the seek-target computation above. */
void w_progress_bar_set_progress(w_progress_bar_t *w, uint16_t permille, const char *elapsed,
                                     const char *remaining, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* W_PROGRESS_BAR_H */
