/*
 * The Spotify pair overlay -- composition only. Presented over the pager by
 * ui_router_present_overlay(), never a fifth page (see ui_router.h).
 *
 * This is the first screen in the project whose mount()/unmount() do real
 * work: they submit INTENT_DOMAIN_PAIRING START/STOP, which is what starts
 * and stops the pairing HTTP listener in main/pair_http.c. The listener's
 * lifetime is exactly this overlay's -- the narrowest window the design could
 * give it, and the thing that makes superseding ADR-0009 defensible.
 *
 * It closes via an explicit button rather than a gesture: navigation in this
 * project is tap-only (w_page_edge_tab.h), and a modal you can only leave by
 * swiping would be the one exception.
 */
#ifndef SCREEN_PAIR_H
#define SCREEN_PAIR_H

#include "intent_submit_types.h"
#include "ui_router.h"
#include "w_qr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Set by ui_app before ui_router_register_overlay() runs. */
    intent_submit_provider_t intent_submit_raw;   /* the RAW provider -- the wrapped one
                                                       sets a screen-wide pending flag meant
                                                       for Now Playing's transport row */
    void                    (*on_close)(void *ctx);   /* ui_app calls the router; this
                                                          screen must not reach into it */
    void                     *on_close_ctx;

    /* Owned by this screen from create() onward. */
    lv_obj_t *headline_label;
    w_qr_t     qr;
    lv_obj_t *url_label;      /* the same URL as text -- fallback when the code won't
                                    scan, and the only way to reach the page by hand */
    lv_obj_t *detail_label;
    lv_obj_t *close_btn;
    lv_obj_t *confirm_btn;     /* "Reconnect a different account" -- only when the
                                    session is already healthy */
} screen_pair_ctx_t;

screen_if_t screen_pair_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_PAIR_H */
