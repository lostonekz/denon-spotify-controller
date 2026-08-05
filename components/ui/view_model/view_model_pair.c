#include "view_model_pair.h"

#include <stdio.h>

/* net_state_t.link_up is a write-once latch -- net_wifi_actions.c sets it
 * true when STA gets an IP and NOTHING ever sets it false again; ipv4 is
 * likewise never cleared. Reading either one alone would confidently print a
 * stale IP for a network this panel has already left, and put a QR on screen
 * pointing at an address that is no longer ours. meta.status is the field
 * that actually goes UNAVAILABLE, which is why view_model_system.c leans on
 * it too. */
static bool network_is_usable(const net_state_t *net)
{
    return view_model_tone_from_link_status(net->meta.status) == TONE_LIVE && net->ipv4 != 0;
}

static pair_ui_stage_t stage_from_session(spotify_session_summary_t status, bool was_pairing)
{
    switch (status) {
    case SPOTIFY_SESSION_PAIRING:
        return PAIR_UI_WAITING;
    case SPOTIFY_SESSION_ACTIVE:
    case SPOTIFY_SESSION_REFRESHING:
        /* A working session. Getting here without having paired in this
         * overlay session means the user opened it on a healthy device, which
         * is the one case that needs an explicit confirm -- re-pairing signs
         * them out first (spotify_session_fsm has no AUTHORIZED->PAIRING
         * edge, so main/pair_http.c does UNPAIR-then-PAIR). */
        return was_pairing ? PAIR_UI_CONNECTED : PAIR_UI_NEEDS_CONFIRM;
    case SPOTIFY_SESSION_UNPAIRED:
        return was_pairing ? PAIR_UI_FAILED : PAIR_UI_READY;
    case SPOTIFY_SESSION_REAUTH_REQUIRED:
    default:
        return PAIR_UI_READY;
    }
}

pair_vm_t view_model_pair_build(const net_state_t *net, const spotify_state_t *spotify,
                                    uint16_t port, bool listener_running, bool was_pairing)
{
    pair_vm_t vm = { 0 };

    if (!network_is_usable(net)) {
        vm.stage = PAIR_UI_NO_NETWORK;
        vm.tone  = TONE_OFFLINE;
        snprintf(vm.headline, sizeof(vm.headline), "No Wi-Fi connection");
        snprintf(vm.detail, sizeof(vm.detail), "Connect this panel to Wi-Fi before pairing Spotify.");
        return vm;
    }

    vm.stage   = stage_from_session(spotify->session_status, was_pairing);
    vm.show_qr = listener_running && vm.stage == PAIR_UI_WAITING;

    if (vm.show_qr) {
        /* ip_info.ip.addr stores network byte order in a little-endian word,
         * so the FIRST octet is the LOW byte -- this is what ESP-IDF's own
         * IP2STR does. Shifting >>24 first renders every address backwards,
         * which is exactly the sort of thing that looks plausible on screen
         * (192.168.1.23 vs 23.1.168.192 both "look like an IP") and then
         * fails silently when someone tries to reach it. */
        uint32_t a = net->ipv4;
        snprintf(vm.url, sizeof(vm.url), "http://%u.%u.%u.%u:%u/pair",
                 (unsigned)(a & 0xFFu), (unsigned)((a >> 8) & 0xFFu),
                 (unsigned)((a >> 16) & 0xFFu), (unsigned)((a >> 24) & 0xFFu),
                 (unsigned)port);
    }

    switch (vm.stage) {
    case PAIR_UI_WAITING:
        vm.tone = TONE_LIVE;
        snprintf(vm.headline, sizeof(vm.headline), "Scan to connect Spotify");
        snprintf(vm.detail, sizeof(vm.detail),
                 "Your phone must be on the same Wi-Fi as this panel.");
        break;
    case PAIR_UI_CONNECTED:
        vm.tone = TONE_LIVE;
        snprintf(vm.headline, sizeof(vm.headline), "Spotify connected");
        snprintf(vm.detail, sizeof(vm.detail), "You're all set.");
        break;
    case PAIR_UI_FAILED:
        vm.tone = TONE_OFFLINE;
        snprintf(vm.headline, sizeof(vm.headline), "That didn't work");
        snprintf(vm.detail, sizeof(vm.detail),
                 "Close and reopen this screen to get a fresh link.");
        break;
    case PAIR_UI_NEEDS_CONFIRM:
        vm.tone                 = TONE_LIVE;
        vm.show_confirm_button = true;
        snprintf(vm.headline, sizeof(vm.headline), "Spotify is connected");
        snprintf(vm.detail, sizeof(vm.detail),
                 "Reconnecting signs you out of the current account first.");
        break;
    case PAIR_UI_READY:
    case PAIR_UI_NO_NETWORK:
    default:
        vm.tone = TONE_STALE;
        snprintf(vm.headline, sizeof(vm.headline), "Getting ready");
        snprintf(vm.detail, sizeof(vm.detail), "Starting the pairing page...");
        break;
    }

    return vm;
}
