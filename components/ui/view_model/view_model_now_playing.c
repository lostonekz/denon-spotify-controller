#include "view_model_now_playing.h"

#include <string.h>

#include "view_model_format.h"
#include "view_model_progress.h"
#include "view_model_text.h"

/* Neutral mid-gray -- matches artwork_palette.c's own fallback for a
 * near-black/near-white/low-saturation cover, reused here for "no artwork
 * held at all" so both "nothing to derive a color from" cases render
 * identically rather than defaulting to black (which would look like a
 * real, chosen color instead of an absence of one). */
#define VM_DEFAULT_ACCENT_RGB565 0x8410u

static void build_title_with_fallback(char *dst, size_t dst_cap,
                                        const char *title, const char *album)
{
    view_model_text_copy_safe(dst, dst_cap, title);
    if (view_model_text_has_content(dst)) {
        return;
    }
    view_model_text_copy_safe(dst, dst_cap, album);
    if (view_model_text_has_content(dst)) {
        return;
    }
    view_model_text_copy_safe(dst, dst_cap, "Unknown Track");
}

now_playing_vm_t view_model_now_playing_build(const playback_state_t *pb,
                                                  const artwork_state_t *art,
                                                  const denon_state_t *denon,
                                                  time_source_t clock,
                                                  bool pending)
{
    now_playing_vm_t vm = { 0 };

    build_title_with_fallback(vm.title, sizeof(vm.title), pb->title, pb->album);
    view_model_text_copy_safe(vm.artist, sizeof(vm.artist), pb->artist);
    view_model_text_copy_safe(vm.album, sizeof(vm.album), pb->album);

    int64_t now_us = clock.now_us(clock.ctx);
    uint32_t displayed_ms = view_model_progress_displayed_ms(pb->position_ms, pb->meta.updated_us,
                                                                 pb->is_playing, now_us);

    view_model_format_mmss(displayed_ms, vm.elapsed, sizeof(vm.elapsed));
    view_model_format_remaining_mmss(displayed_ms, pb->duration_ms, vm.remaining, sizeof(vm.remaining));
    vm.progress_permille = view_model_progress_permille(displayed_ms, pb->duration_ms);

    vm.is_playing = pb->is_playing;
    vm.pending    = pending;
    vm.tone       = view_model_tone_from_link_status(pb->meta.status);

    /* generation == 0 means "no artwork held at all" -- core_pool_slot_t's
     * own contract: generation bumps on acquire, so a genuinely acquired
     * handle is never generation 0 (artwork_service.c already relies on
     * this exact check for the same reason). But a held handle is not
     * enough on its own -- svc_artwork and svc_spotify are two different
     * tasks publishing to two different domains at their own pace, so the
     * held artwork can be for a track that has already changed again by
     * the time this snapshot is read. Only trust the accent when
     * art->track_id positively matches the track actually playing right
     * now; otherwise fall back, same as "no artwork held." */
    bool artwork_matches_current_track = (art->handle.generation != 0) &&
                                          (pb->track_id[0] != '\0') &&
                                          (strcmp(art->track_id, pb->track_id) == 0);
    vm.accent_rgb565 = artwork_matches_current_track
                            ? (uint16_t)art->dominant_color_rgb565
                            : VM_DEFAULT_ACCENT_RGB565;

    view_model_format_half_db(denon->volume_half_db, vm.volume_display, sizeof(vm.volume_display));
    vm.volume_tone = view_model_tone_from_link_status(denon->meta.status);

    view_model_text_copy_safe(vm.device_name, sizeof(vm.device_name), pb->device_name);
    if (!view_model_text_has_content(vm.device_name)) {
        view_model_text_copy_safe(vm.device_name, sizeof(vm.device_name), "No Device");
    }

    return vm;
}
