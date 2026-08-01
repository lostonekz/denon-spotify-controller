/*
 * now_playing_vm_t and its pure builder -- the load-bearing translation
 * layer UI_ARCHITECTURE.md §1 describes ("the only place domain data
 * becomes display data"). Pure, host-tested, zero LVGL/ESP-IDF -- Tier 1.
 *
 * Deliberately NOT UI_ARCHITECTURE.md §1's own now_playing_vm_t verbatim:
 * that struct's title/artist/album are char[96] (an ungrounded number,
 * unrelated to the real source field sizes) and its accent field is
 * lv_color_t (an LVGL type, incompatible with a host-testable pure
 * builder). This version sizes text fields from the real source
 * (playback_state_t's own PLAYBACK_*_MAX, all 64) and represents accent
 * as RGB565 -- the same representation artwork_state_t.dominant_color_rgb565
 * already uses -- converted to lv_color_t only by a thin target-only
 * wrapper at the point a widget actually renders it (Tier 6). Also adds
 * two fields UI_ARCHITECTURE.md's struct never had at all: volume_display/
 * volume_tone, for the read-only Denon volume badge Art-forward's mock
 * includes (Phase 5 plan §6). UI_ARCHITECTURE.md is corrected to match in
 * Tier 8.
 */
#ifndef VIEW_MODEL_NOW_PLAYING_H
#define VIEW_MODEL_NOW_PLAYING_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state_types.h"   /* playback_state_t, artwork_state_t, denon_state_t */
#include "os_if_types.h"       /* time_source_t */
#include "view_model_tone.h"   /* tone_t */

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
    bool      is_playing;
    bool      pending;             /* optimistic intent in flight -- caller-tracked,
                                     * not derived from the state snapshots below
                                     * (EVENTS_AND_STATE.md §6.3: "pending state
                                     * lives in the view model, not the state store") */
    tone_t    tone;
    uint16_t  accent_rgb565;       /* from artwork_state_t.dominant_color_rgb565, or a
                                     * neutral default when no artwork is currently held */
    char      volume_display[VM_VOLUME_DISPLAY_MAX];   /* "52.5" -- Denon's own volume,
                                                         * read-only badge (Phase 5 plan §6) */
    tone_t    volume_tone;         /* Denon's own honesty check, same mapping as `tone` */
} now_playing_vm_t;

/* The pure core: no core_state_read() inside, every input arrives as a
 * plain parameter. pb/art/denon are plain snapshots (a memcpy'd
 * core_state_read() is already how the caller obtains these); clock is
 * the same injected time_source_t spotify_progress.c already takes;
 * pending reflects whatever optimistic-intent bookkeeping the caller
 * (target-only glue, Tier 6) is doing -- this function does not compute
 * it, it just carries whatever the caller already knows. */
now_playing_vm_t view_model_now_playing_build(const playback_state_t *pb,
                                                  const artwork_state_t *art,
                                                  const denon_state_t *denon,
                                                  time_source_t clock,
                                                  bool pending);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_NOW_PLAYING_H */
