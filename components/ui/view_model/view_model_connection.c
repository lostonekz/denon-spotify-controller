#include "view_model_connection.h"

#include <stdio.h>

#include "view_model_format.h"

connection_vm_t view_model_connection_build(const denon_state_t *denon)
{
    connection_vm_t vm = { 0 };

    if (denon->host[0] != '\0') {
        snprintf(vm.host, sizeof(vm.host), "%s", denon->host);
    } else {
        snprintf(vm.host, sizeof(vm.host), "Not set");
    }

    if (denon->round_trip_ms > 0) {
        snprintf(vm.round_trip_display, sizeof(vm.round_trip_display), "%u ms", (unsigned)denon->round_trip_ms);
    } else {
        snprintf(vm.round_trip_display, sizeof(vm.round_trip_display), "--");
    }

    vm.link_tone = view_model_tone_from_link_status(denon->meta.status);
    vm.telnet_selected = vm.link_tone == TONE_LIVE;
    vm.http_selected   = !vm.telnet_selected;
    snprintf(vm.link_label, sizeof(vm.link_label), "%s", vm.telnet_selected ? "Linked" : "Fallback");

    vm.volume_limit_half_db_raw = denon->volume_limit_half_db;
    view_model_format_half_db(denon->volume_limit_half_db, vm.volume_limit_display, sizeof(vm.volume_limit_display));

    return vm;
}
