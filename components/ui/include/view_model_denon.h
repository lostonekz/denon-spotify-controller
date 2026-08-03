/*
 * Pure builder for the Receiver screen -- (denon_state_t, playback_state_t)
 * -> denon_vm_t. Zero LVGL/ESP-IDF, host-tested (test_view_model_denon.c),
 * same Tier-1 shape as view_model_now_playing_build().
 *
 * Reads BOTH domains directly (same "one pure builder reads what it needs"
 * shape view_model_now_playing_build() already uses) rather than a new
 * main/-layer cross-domain resolver -- this is a display decision (what
 * text/highlight to show), not an intent-routing one like adr/0007's volume
 * coordinator.
 */
#ifndef VIEW_MODEL_DENON_H
#define VIEW_MODEL_DENON_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state_types.h"
#include "view_model_tone.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VM_DENON_POWER_LABEL_MAX 24   /* "The receiver's off" + NUL, rounded up */
#define VM_DENON_SUBTITLE_MAX    24   /* "HEOS - playing" (the real text uses U+00B7) + NUL,
                                          rounded up; also fits the TV card's static "HDMI ARC" */
#define VM_DENON_VOLUME_DISPLAY_MAX 8   /* "98.5" + NUL -- same value as view_model_now_playing.h's
                                            own VM_VOLUME_DISPLAY_MAX, duplicated rather than
                                            coupling this screen's vm to Now Playing's header */

typedef struct {
    /* denon->meta.version != 0 -- core_state_init() zero-memsets every
     * domain, and a never-written denon_state_t reads as power_on=false,
     * link_status_t=LINK_STATUS_OK (0), input_id=0 -- indistinguishable
     * from a real, confirmed-off receiver on DENON_INPUT_PHONO unless this
     * is checked. version starts at 0 and every real core_state_write()
     * sets it to at least 1 (core_state.c: next_version = meta->version+1),
     * so this is an exact, clock-independent "have we ever heard from
     * svc_denon" signal -- real, found live: without it, the Receiver
     * screen showed "The receiver is off" during svc_denon's own real
     * discovery+connect window (~7s after boot, denon_service_io.c), which
     * reads as a wrong, alarming claim rather than "still connecting". */
    bool     has_real_state;
    bool     power_on;
    tone_t   denon_tone;
    char     power_label[VM_DENON_POWER_LABEL_MAX];   /* "Connecting..." until has_real_state */

    bool     net_selected;   /* denon->input_id == DENON_INPUT_NET */
    bool     tv_selected;    /* denon->input_id == DENON_INPUT_TV -- both false is a real,
                                  valid state (receiver on some other input), not an error */
    char     net_subtitle[VM_DENON_SUBTITLE_MAX];

    uint16_t volume_half_db_raw;
    char     volume_display[VM_DENON_VOLUME_DISPLAY_MAX];
} denon_vm_t;

denon_vm_t view_model_denon_build(const denon_state_t *denon, const playback_state_t *pb);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_DENON_H */
