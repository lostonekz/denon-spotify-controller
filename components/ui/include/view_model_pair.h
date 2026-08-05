/*
 * Pure builder for the Spotify pair overlay -- (net_state_t, spotify_state_t)
 * plus a couple of plain values the target-only apply-glue supplies ->
 * pair_vm_t. Zero LVGL/ESP-IDF, host-tested.
 *
 * The QR this feeds encodes only "http://<ip>:<port>/pair" -- the panel's own
 * page. The ~320-char Spotify authorize URL never comes near the UI: it is
 * generated in main/pair_http.c and rendered as a link by that page. There is
 * no field for it in spotify_state_t and event_t caps at 64 bytes, so there is
 * no path for it to get here anyway -- and putting a 512-byte string in a
 * struct the UI memcpy's every frame would cost real per-frame time.
 *
 * `was_pairing` is a latch (did this overlay session ever reach PAIRING?) and
 * is therefore stateful, so it arrives as a parameter rather than living here
 * -- same reason view_model_system_build() takes wifi_rssi_dbm instead of
 * calling esp_wifi_sta_get_ap_info() itself. It is what distinguishes
 * "UNPAIRED because we never paired" from "UNPAIRED because the exchange just
 * failed", which are the same value of session_status but very different
 * things to say to someone standing at the panel.
 */
#ifndef VIEW_MODEL_PAIR_H
#define VIEW_MODEL_PAIR_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state_types.h"
#include "view_model_tone.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VM_PAIR_URL_MAX       40   /* "http://255.255.255.255:65535/pair" is 33 + NUL */
#define VM_PAIR_HEADLINE_MAX  48
#define VM_PAIR_DETAIL_MAX    96

typedef enum {
    PAIR_UI_NO_NETWORK = 0,   /* no usable link/IP -- we cannot host the page at all */
    PAIR_UI_READY,            /* listener coming up; not yet PAIRING */
    PAIR_UI_WAITING,          /* PAIRING -- QR is live, waiting for the paste */
    PAIR_UI_CONNECTED,        /* ACTIVE, and this overlay session got it there */
    PAIR_UI_FAILED,           /* UNPAIRED after having been PAIRING -- exchange failed */
    PAIR_UI_NEEDS_CONFIRM,    /* already connected; re-pairing needs an explicit confirm */
} pair_ui_stage_t;

typedef struct {
    pair_ui_stage_t stage;
    bool            show_qr;
    bool            show_confirm_button;
    char            url[VM_PAIR_URL_MAX];             /* "" unless show_qr */
    char            headline[VM_PAIR_HEADLINE_MAX];
    char            detail[VM_PAIR_DETAIL_MAX];
    tone_t          tone;
} pair_vm_t;

/* `listener_running` is app-layer truth relayed by the apply-glue: has the
 * pairing HTTP server actually been asked for and come up? The QR must never
 * be shown pointing at a port with nothing behind it.
 * `was_pairing` -- see the file header. */
pair_vm_t view_model_pair_build(const net_state_t *net, const spotify_state_t *spotify,
                                    uint16_t port, bool listener_running, bool was_pairing);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_PAIR_H */
