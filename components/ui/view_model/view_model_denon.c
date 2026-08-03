#include "view_model_denon.h"

#include <stdio.h>

#include "denon_intent_types.h"
#include "view_model_format.h"

denon_vm_t view_model_denon_build(const denon_state_t *denon, const playback_state_t *pb)
{
    denon_vm_t vm = { 0 };

    vm.has_real_state = denon->meta.version != 0;
    vm.power_on          = denon->power_on;
    vm.denon_tone        = view_model_tone_from_link_status(denon->meta.status);

    if (!vm.has_real_state) {
        snprintf(vm.power_label, sizeof(vm.power_label), "Connecting...");
    } else {
        snprintf(vm.power_label, sizeof(vm.power_label), "%s",
                 vm.power_on ? "The receiver is on" : "The receiver is off");
    }

    vm.net_selected = denon->input_id == DENON_INPUT_NET;
    vm.tv_selected  = denon->input_id == DENON_INPUT_TV;

    /* input_id == NET alone doesn't prove Spotify is actually playing over
     * it -- the receiver could be sitting on that input from a prior
     * session while playback moved elsewhere. Only claim "playing" once
     * both signals agree (device_is_avr confirmed AND is_playing), same
     * honesty-first precedent view_model_tone.c already established for
     * link health. */
    if (!vm.has_real_state) {
        snprintf(vm.net_subtitle, sizeof(vm.net_subtitle), "Connecting...");
    } else if (!vm.net_selected) {
        snprintf(vm.net_subtitle, sizeof(vm.net_subtitle), "HEOS");
    } else if (pb->device_is_avr == DEVICE_IS_AVR_YES && pb->is_playing) {
        snprintf(vm.net_subtitle, sizeof(vm.net_subtitle), "HEOS \xc2\xb7 playing");
    } else if (pb->device_is_avr == DEVICE_IS_AVR_YES) {
        snprintf(vm.net_subtitle, sizeof(vm.net_subtitle), "HEOS \xc2\xb7 paused");
    } else {
        snprintf(vm.net_subtitle, sizeof(vm.net_subtitle), "HEOS \xc2\xb7 idle");
    }

    view_model_format_half_db(denon->volume_half_db, vm.volume_display, sizeof(vm.volume_display));
    vm.volume_half_db_raw = denon->volume_half_db;

    return vm;
}
