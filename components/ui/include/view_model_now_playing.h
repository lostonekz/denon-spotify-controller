/*
 * now_playing_vm_t and its pure builder -- the load-bearing translation
 * layer UI_ARCHITECTURE.md §1 describes ("the only place domain data
 * becomes display data"). Pure, host-tested, zero LVGL/ESP-IDF -- Tier 1.
 *
 * Deliberately NOT UI_ARCHITECTURE.md §1's own now_playing_vm_t verbatim:
 * that struct's title/artist/album are char[96] (an ungrounded number,
 * unrelated to the real source field sizes). This version sizes text
 * fields from the real source (playback_state_t's own PLAYBACK_*_MAX, all
 * 64). Also adds fields UI_ARCHITECTURE.md's struct never had at all:
 * volume_display/volume_half_db_raw, for the Art-forward redesign's real,
 * live volume slider (Phase 5 addendum). UI_ARCHITECTURE.md is corrected
 * to match in Tier 8.
 *
 * tone/volume_tone/denon_tone/power_on/device_name/show_bring_here all
 * came and went with the header controls that were their only consumers
 * (w_status_pill, then a header device-name label, then a power glyph +
 * "Bring Here" tap zone) -- removed for the last time, live, once pressing
 * Play was confirmed to already wake the AVR and resume playback there,
 * making a separate standing header control redundant
 * (screen_now_playing.h's own top comment). No STALE/OFFLINE health
 * signal is currently visible anywhere on this screen as a result -- named
 * here, not silently lost.
 *
 * accent_rgb565 (the old per-track dynamic accent, from artwork_state_t.
 * dominant_color_rgb565) was REMOVED by the Art-forward redesign -- its
 * only two consumers (the scrub bar's fill color, the removed scrim's own
 * contrast logic) are both gone: the scrub bar is flat white per the
 * design spec ("no scrim, no gradient, no vignette... traded away for the
 * art"), not art-derived. artwork_state_t.dominant_color_rgb565 itself and
 * svc_artwork's computation of it are untouched -- that's real,
 * general-purpose core_state data with its own service-level existence,
 * not something this UI-only redesign reaches upstream to delete.
 */
#ifndef VIEW_MODEL_NOW_PLAYING_H
#define VIEW_MODEL_NOW_PLAYING_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state_types.h"   /* playback_state_t, artwork_state_t, denon_state_t */
#include "os_if_types.h"       /* time_source_t */

#ifdef __cplusplus
extern "C" {
#endif

#define VM_ELAPSED_MAX        8    /* "3:07"   + NUL */
#define VM_REMAINING_MAX      8    /* "-1:24"  + NUL */
#define VM_VOLUME_DISPLAY_MAX 8    /* "98.5"   + NUL */

typedef struct {
    char      title[PLAYBACK_TITLE_MAX];
    char      artist[PLAYBACK_ARTIST_MAX];
    char      album[PLAYBACK_ALBUM_MAX];
    char      elapsed[VM_ELAPSED_MAX];
    char      remaining[VM_REMAINING_MAX];
    uint16_t  progress_permille;   /* 0..1000, already interpolated */
    uint32_t  duration_ms;         /* pb->duration_ms, passed straight through -- w_progress_bar
                                     * needs the raw value (not just the formatted elapsed/
                                     * remaining strings) to convert a drag position back into
                                     * an absolute seek target */
    bool      is_playing;
    bool      pending;             /* optimistic intent in flight -- caller-tracked,
                                     * not derived from the state snapshots below
                                     * (EVENTS_AND_STATE.md §6.3: "pending state
                                     * lives in the view model, not the state store") */
    char      volume_display[VM_VOLUME_DISPLAY_MAX];   /* "52.5" -- Denon's own volume */
    uint16_t  volume_half_db_raw;  /* Denon's own volume, raw half-dB -- seeds
                                     * w_volume_slider's track position (volume_display
                                     * is the formatted string, this is the number it
                                     * came from; a widget needs both) */
    bool      has_active_device;   /* pb->device_id non-empty -- Spotify has some device
                                     * targeted, even if paused. False means the Play tap
                                     * needs to wake the AVR + PLAYBACK_INTENT_RESUME_ON_AVR
                                     * instead of a plain PLAYBACK_INTENT_PLAY, which would
                                     * just 404 with nothing to target (w_transport.c). */
} now_playing_vm_t;

/* The pure core: no core_state_read() inside, every input arrives as a
 * plain parameter. pb/denon are plain snapshots (a memcpy'd
 * core_state_read() is already how the caller obtains these); clock is
 * the same injected time_source_t spotify_progress.c already takes;
 * pending reflects whatever optimistic-intent bookkeeping the caller
 * (target-only glue, Tier 6) is doing -- this function does not compute
 * it, it just carries whatever the caller already knows.
 *
 * No longer takes artwork_state_t -- its only consumer (the old per-track
 * accent_rgb565 field) was removed by the Art-forward redesign (see this
 * header's own top comment). screen_now_playing_update_t (screen_now_
 * playing.h) still carries the artwork handle/track_id the *widget* layer
 * needs (w_art_card's pixel fetch) -- that's a separate, wider payload the
 * caller builds alongside this pure vm, not part of it. */
now_playing_vm_t view_model_now_playing_build(const playback_state_t *pb,
                                                  const denon_state_t *denon,
                                                  time_source_t clock,
                                                  bool pending);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_NOW_PLAYING_H */
