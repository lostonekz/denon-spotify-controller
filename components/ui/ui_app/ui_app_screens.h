/*
 * Screen/overlay registration and apply-glue wiring, split out of ui_app.c
 * for the 400-line gate -- same reason ui_app_watchdog.c was.
 *
 * This is where every "which provider does this screen get, and why" decision
 * lives. ui_app.c keeps the LVGL/HAL bring-up and the task loop.
 */
#ifndef UI_APP_SCREENS_H
#define UI_APP_SCREENS_H

#include <stdbool.h>

#include "ui_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers all four screens plus the pair overlay and initialises every
 * apply-glue. Requires app->router to already exist. Logs and returns false
 * on the first failure. */
bool ui_app_screens_create(ui_app_t *app, const ui_app_cfg_t *cfg);

/* One tick of every apply-glue. Each drains its own subscriptions
 * unconditionally, so none of their event rings can fill while their screen
 * is off-view or their overlay closed. */
void ui_app_screens_tick(ui_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* UI_APP_SCREENS_H */
