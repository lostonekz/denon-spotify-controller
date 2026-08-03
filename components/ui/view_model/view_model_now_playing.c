#include "view_model_now_playing.h"

#include <string.h>

#include "view_model_format.h"
#include "view_model_progress.h"
#include "view_model_text.h"

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
    view_model_text_copy_safe(dst, dst_cap, "--");
}

now_playing_vm_t view_model_now_playing_build(const playback_state_t *pb,
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
    vm.duration_ms          = pb->duration_ms;

    vm.is_playing = pb->is_playing;
    vm.pending    = pending;

    view_model_format_half_db(denon->volume_half_db, vm.volume_display, sizeof(vm.volume_display));
    vm.volume_half_db_raw = denon->volume_half_db;

    vm.has_active_device = pb->device_id[0] != '\0';   /* the empty-on-NO_ACTIVE_DEVICE
                                                         * playback_state_t handle_poll_result()
                                                         * writes (spotify_service_task.c) leaves
                                                         * this empty too -- the field that's
                                                         * never populated with a placeholder
                                                         * anywhere else, so it's the direct
                                                         * signal to key off. */

    return vm;
}
