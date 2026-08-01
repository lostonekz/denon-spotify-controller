/*
 * Full-bleed album art + integrated adaptive scrim (UI_ARCHITECTURE.md §4;
 * Phase 5 plan §7: under Art-forward the card IS the backdrop, so a
 * separate w_backdrop is never built). Crossfades in (THEME_MOTION_SLOW)
 * over a neutral placeholder (plan §3) rather than popping; the scrim's
 * bottom-stop opacity adapts to the artwork's own bottom-band luma (plan
 * §5, view_model_scrim.h) so pale covers still read legibly.
 *
 * Copies pixels via artwork_pixel_provider_t.lock_copy() into a buffer this
 * widget owns (plan §1) -- never holds a pointer into svc_artwork's pool.
 */
#ifndef W_ART_CARD_H
#define W_ART_CARD_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#include "app_state_types.h"      /* PLAYBACK_TRACK_ID_MAX */
#include "artwork_pixel_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    artwork_pixel_provider_t artwork_pixels;
} w_art_card_cfg_t;

typedef struct {
    lv_obj_t         *root;
    lv_obj_t         *placeholder;
    lv_obj_t         *image;
    lv_obj_t         *scrim;
    lv_image_dsc_t     img_dsc;
    uint16_t             *pixel_buf;   /* PSRAM-backed (heap_caps_malloc on target,
                                          * plain malloc on the host harness) --
                                          * ARTWORK_PIXEL_BYTES, this widget's own,
                                          * per artwork_pixel_provider_t's contract */
    artwork_pixel_provider_t artwork_pixels;
    artwork_handle_t           current_handle;    /* {0,0} == placeholder showing */
    char                          current_track_id[PLAYBACK_TRACK_ID_MAX];
} w_art_card_t;

/* NULL if the pixel buffer allocation fails (PSRAM/heap exhaustion) --
 * propagates up through screen_now_playing_get_interface()'s create() the
 * same way any other screen-build failure does. */
lv_obj_t *w_art_card_create(lv_obj_t *parent, w_art_card_t *w, const w_art_card_cfg_t *cfg);

/* Safe to call every screen update() tick, not just on a real change --
 * no-ops internally unless track_id or handle actually differ from what's
 * already applied. handle == {0,0} means "no artwork for this track" and
 * resets to the placeholder (Tier -1's clear-on-empty fix, rendered here). */
void w_art_card_set_artwork(w_art_card_t *w, artwork_handle_t handle, const char *track_id);

#ifdef __cplusplus
}
#endif

#endif /* W_ART_CARD_H */
