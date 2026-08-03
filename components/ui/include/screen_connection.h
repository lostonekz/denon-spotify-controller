/*
 * The Connection screen -- composition only, same discipline screen_denon.c's
 * own top comment states. Plain designed dark background, same as Denon/
 * Receiver -- no stroke-label/marquee/scrim machinery needed here either.
 * connection_vm_t (view_model_connection.h) is used directly as the
 * update() payload, no wider wrapper struct.
 */
#ifndef SCREEN_CONNECTION_H
#define SCREEN_CONNECTION_H

#include "intent_submit_types.h"
#include "ui_router.h"
#include "w_info_row.h"
#include "w_source_status_card.h"
#include "w_volume_cap_slider.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Set by the caller (ui_app.c) before ui_router_register_screen();
     * create() reads this once and never again. Every control on this
     * screen is a single discrete Denon/settings action with near-instant
     * feedback -- no wrapped/pending-tracked provider needed, same
     * reasoning as screen_denon.c's own intent_submit_raw. */
    intent_submit_provider_t intent_submit_raw;

    /* Owned by this screen from create() onward. */
    w_info_row_t            header_pill;      /* "CONNECTION" / ". Linked" */
    w_info_row_t            row_host;         /* "Receiver" / host */
    w_info_row_t            row_round_trip;   /* "Round trip" / "38 ms" */
    w_source_status_card_t  telnet_card;
    w_source_status_card_t  http_card;
    w_volume_cap_slider_t   volume_cap;
} screen_connection_ctx_t;

screen_if_t screen_connection_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_CONNECTION_H */
