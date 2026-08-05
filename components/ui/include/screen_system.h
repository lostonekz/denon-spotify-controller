/*
 * The System screen -- composition only. Every row is read-only, so it takes
 * no intent_submit_provider_t. Plain designed dark background, same as every
 * other Phase 6 screen.
 *
 * One exception to "read-only": the Spotify row is a tap target. It is the
 * only screen that can report "Reauth needed", and until the pair overlay
 * existed that was a dead end -- a real problem stated in words with nothing
 * to do about it. Tapping it presents the pair overlay via the callback
 * below; this screen deliberately does not hold a ui_router_t itself, so
 * presentation stays owned by whoever owns the router.
 */
#ifndef SCREEN_SYSTEM_H
#define SCREEN_SYSTEM_H

#include "ui_router.h"
#include "w_info_row.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Set by ui_app before registration -- create() wires it onto the row. */
    void (*on_spotify_activate)(void *ctx);
    void  *on_spotify_activate_ctx;

    /* Owned by this screen from create() onward. */
    lv_obj_t     *verdict_label;   /* "Everything's working" / "Something needs attention" */
    w_info_row_t   row_wifi;
    w_info_row_t   row_receiver;
    w_info_row_t   row_spotify;
    w_info_row_t   row_firmware;
    w_info_row_t   row_uptime;
    w_info_row_t   row_panel;
} screen_system_ctx_t;

screen_if_t screen_system_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SYSTEM_H */
