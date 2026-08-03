/*
 * The Art-forward Now Playing screen (UI_ARCHITECTURE.md §6, Phase 5 plan's
 * Context section) -- composition only, per §1: no formatting, no business
 * rules, no drawing of its own, just widget setters fed from the vm.
 *
 * Art-forward redesign: w_status_pill is dropped from this screen entirely.
 * Its STALE/OFFLINE signal originally got folded into a header device-name
 * label's own tone-coloring, but that label (and the power glyph beside
 * it) were removed too, per later live feedback: pressing Play already
 * wakes the AVR and resumes playback there when nothing's active
 * (w_transport.c, PLAYBACK_INTENT_RESUME_ON_AVR), making a separate
 * standing header control redundant. Neither widget file is deleted
 * (unlike w_page_dots) -- both stay unused here as likely reusable
 * infrastructure for the real Denon/Settings screens Phase 6 will build.
 */
#ifndef SCREEN_NOW_PLAYING_H
#define SCREEN_NOW_PLAYING_H

#include "app_state_types.h"
#include "artwork_pixel_types.h"
#include "intent_submit_types.h"
#include "ui_router.h"
#include "view_model_now_playing.h"
#include "w_art_card.h"
#include "w_marquee_label.h"
#include "w_progress_bar.h"
#include "w_stroke_label.h"
#include "w_toast.h"
#include "w_transport.h"
#include "w_volume_slider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* now_playing_vm_t (Tier 1's pure struct) deliberately doesn't carry the
 * raw artwork handle -- it's pure/host-tested with no widget consumer to
 * justify one. w_art_card needs the real handle+track_id to fetch pixels,
 * so this is the wider payload this screen's update() actually expects;
 * view_model_now_playing_apply.c builds one of these each tick, not a bare
 * now_playing_vm_t. */
typedef struct {
    now_playing_vm_t vm;
    artwork_handle_t    artwork_handle;
    char                    artwork_track_id[PLAYBACK_TRACK_ID_MAX];    /* from artwork_state_t --
                                                                          * lags behind playback_track_id
                                                                          * below until the new track's
                                                                          * artwork actually finishes
                                                                          * decoding (up to ~1.8s, R1) */
    char                       playback_track_id[PLAYBACK_TRACK_ID_MAX];   /* from playback_state_t --
                                                                             * changes the instant Spotify
                                                                             * reports a new track. w_art_card
                                                                             * uses THIS to drop stale artwork
                                                                             * immediately on track change,
                                                                             * not artwork_track_id's own
                                                                             * lagging value (see w_art_card.c) */
} screen_now_playing_update_t;

typedef struct {
    /* Set by the caller (ui_app.c) before ui_router_register_screen();
     * create() reads these once and never again. */
    artwork_pixel_provider_t artwork_pixels;
    intent_submit_provider_t intent_submit;         /* wrapped -- pending-tracked, used by
                                                          w_transport (the only domain the
                                                          pending/toast/ACK machinery actually
                                                          exists for) */
    intent_submit_provider_t intent_submit_raw;   /* unwrapped -- used by w_volume_slider,
                                                          on a domain that shouldn't fight the
                                                          transport row's own pending/dim state
                                                          (real bug found during planning -- see
                                                          the redesign plan's own hardware
                                                          critique) */

    /* Owned by this screen from create() onward. */
    w_art_card_t          art_card;
    w_marquee_label_t        title_label;
    w_stroke_label_t                  artist_label;
    w_progress_bar_t                     progress_bar;
    w_transport_t                           transport;
    w_volume_slider_t                          volume;
    w_toast_t                                     toast;
} screen_now_playing_ctx_t;

screen_if_t screen_now_playing_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_NOW_PLAYING_H */
