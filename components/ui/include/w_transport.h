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
} w_transport_cfg_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *btn_prev;
    lv_obj_t *btn_play_pause;
    lv_obj_t *btn_next;
    lv_obj_t *play_pause_label;
    intent_submit_provider_t intent_submit;
    bool                        is_playing;
} w_transport_t;

lv_obj_t *w_transport_create(lv_obj_t *parent, w_transport_t *w, const w_transport_cfg_t *cfg);

/* is_playing drives the play/pause glyph, always from the real state
 * store's own is_playing (view_model_now_playing_build() never overrides
 * it optimistically -- a scoped Tier 6 decision: EVENTS_AND_STATE.md
 * §6.3's "immediately renders the paused state" is satisfied here by the
 * pending dim/disable treatment below, a real and visible response to the
 * tap, without also building full per-field optimistic-value overriding,
 * which nothing else in Tier 6 needs). pending dims the whole row and
 * makes it non-interactive -- that alone is what changes immediately on
 * tap; the glyph itself only changes once the real state store agrees. */
void w_transport_set_state(w_transport_t *w, bool is_playing, bool pending);

#ifdef __cplusplus
}
#endif

#endif /* W_TRANSPORT_H */
