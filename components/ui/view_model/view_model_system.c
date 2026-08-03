#include "view_model_system.h"

#include <stdio.h>

#include "view_model_format.h"

static void spotify_status_text_and_tone(spotify_session_summary_t status, char *buf, size_t buflen, tone_t *out_tone)
{
    switch (status) {
    case SPOTIFY_SESSION_ACTIVE:
        snprintf(buf, buflen, "Connected");
        *out_tone = TONE_LIVE;
        break;
    case SPOTIFY_SESSION_PAIRING:
        snprintf(buf, buflen, "Pairing...");
        *out_tone = TONE_STALE;
        break;
    case SPOTIFY_SESSION_REFRESHING:
        snprintf(buf, buflen, "Renewing");
        *out_tone = TONE_STALE;
        break;
    case SPOTIFY_SESSION_REAUTH_REQUIRED:
        snprintf(buf, buflen, "Reauth needed");
        *out_tone = TONE_OFFLINE;
        break;
    case SPOTIFY_SESSION_UNPAIRED:
    default:
        snprintf(buf, buflen, "Not paired");
        *out_tone = TONE_OFFLINE;
        break;
    }
}

system_vm_t view_model_system_build(const net_state_t *net, const denon_state_t *denon,
                                        const spotify_state_t *spotify, int8_t wifi_rssi_dbm,
                                        const char *fw_version, const char *fw_date,
                                        const char *panel_id, time_source_t clock)
{
    system_vm_t vm = { 0 };

    vm.wifi_tone = view_model_tone_from_link_status(net->meta.status);
    if (vm.wifi_tone == TONE_LIVE && net->link_up) {
        snprintf(vm.wifi_display, sizeof(vm.wifi_display), "%d dBm", (int)wifi_rssi_dbm);
    } else if (vm.wifi_tone == TONE_OFFLINE) {
        snprintf(vm.wifi_display, sizeof(vm.wifi_display), "Disconnected");
    } else {
        snprintf(vm.wifi_display, sizeof(vm.wifi_display), "Reconnecting...");
    }

    vm.denon_tone = view_model_tone_from_link_status(denon->meta.status);
    if (denon->round_trip_ms > 0) {
        snprintf(vm.denon_display, sizeof(vm.denon_display), "%u ms", (unsigned)denon->round_trip_ms);
    } else {
        snprintf(vm.denon_display, sizeof(vm.denon_display), "--");
    }

    spotify_status_text_and_tone(spotify->session_status, vm.spotify_display, sizeof(vm.spotify_display),
                                  &vm.spotify_tone);

    /* "Everything's working" only when all three real signals agree --
     * anything else gets a single generic "needs attention" phrase rather
     * than trying to enumerate every possible combination in prose. */
    if (vm.wifi_tone == TONE_LIVE && vm.denon_tone == TONE_LIVE && vm.spotify_tone == TONE_LIVE) {
        snprintf(vm.verdict, sizeof(vm.verdict), "Everything's working");
    } else {
        snprintf(vm.verdict, sizeof(vm.verdict), "Something needs attention");
    }

    snprintf(vm.firmware_display, sizeof(vm.firmware_display), "%s \xc2\xb7 %s", fw_version, fw_date);

    int64_t now_us = clock.now_us(clock.ctx);
    view_model_format_uptime((uint32_t)(now_us / 1000000), vm.uptime_display, sizeof(vm.uptime_display));

    snprintf(vm.panel_id, sizeof(vm.panel_id), "%s", panel_id);

    return vm;
}
