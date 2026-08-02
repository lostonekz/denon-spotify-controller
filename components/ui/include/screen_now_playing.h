/*
 * The Art-forward Now Playing screen (UI_ARCHITECTURE.md §6, Phase 5 plan's
 * Context section) -- composition only, per §1: no formatting, no business
 * rules, no drawing of its own, just widget setters fed from the vm.
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
#include "w_status_pill.h"
#include "w_toast.h"
#include "w_transport.h"

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
    intent_submit_provider_t intent_submit;

    /* Owned by this screen from create() onward. */
    w_art_card_t          art_card;
    w_marquee_label_t     title_label;
    lv_obj_t                 *artist_label;
    w_progress_bar_t         progress_bar;
    w_transport_t             transport;
    w_status_pill_t             status_pill;
    w_toast_t                     toast;
    lv_obj_t                        *device_label;   /* top-right overlay slot -- was a live
                                                       * clock, now the active Spotify Connect
                                                       * device name (u->vm.device_name),
                                                       * read-only display */
    lv_obj_t                           *volume_label;
} screen_now_playing_ctx_t;

screen_if_t screen_now_playing_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_NOW_PLAYING_H */
