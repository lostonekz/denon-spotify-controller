/*
 * The System screen -- composition only. Fully read-only, no controls at
 * all, so it takes no intent_submit_provider_t. Plain designed dark
 * background, same as every other Phase 6 screen.
 */
#ifndef SCREEN_SYSTEM_H
#define SCREEN_SYSTEM_H

#include "ui_router.h"
#include "w_info_row.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
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
