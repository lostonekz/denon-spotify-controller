/*
 * A plain "label ... value" row -- covers every row on the Connection and
 * System screens: Connection's "Receiver"/"Round trip" card rows and its
 * own "CONNECTION / . Linked" header pill (dot enabled); System's Wi-Fi/
 * Receiver/Spotify status rows (dot enabled) and Firmware/Awake-for/Panel
 * rows (dot disabled). One widget for both the plain and status-with-dot
 * shape rather than two near-duplicates -- the only real difference is
 * whether a small leading status dot exists and whether the value gets
 * tone-colored.
 */
#ifndef W_INFO_ROW_H
#define W_INFO_ROW_H

#include <stdbool.h>

#include "lvgl.h"

#include "view_model_tone.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *label;         /* static per instance, set once at creation */
    bool        show_status_dot;
} w_info_row_cfg_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *left_group;
    lv_obj_t *dot;      /* NULL if cfg->show_status_dot was false */
    lv_obj_t *label;
    lv_obj_t *value;
} w_info_row_t;

lv_obj_t *w_info_row_create(lv_obj_t *parent, w_info_row_t *w, const w_info_row_cfg_t *cfg);

/* Plain value text, no dot/tone coloring -- THEME_COLOR_TEXT_PRIMARY. Used
 * for rows with no status dot (Firmware/Awake for/Panel, Connection's
 * Receiver/Round trip). */
void w_info_row_set_value(w_info_row_t *w, const char *value);

/* Colors the dot (if this row was created with show_status_dot) and the
 * value text together from tone -- TONE_LIVE green, TONE_STALE amber,
 * TONE_OFFLINE red (THEME_COLOR_DANGER -- these rows mean "can we reach
 * it," not "did the user turn it off", so red is the more honest color
 * here than w_power_glyph's own off-is-gray convention). */
void w_info_row_set_status(w_info_row_t *w, const char *value, tone_t tone);

#ifdef __cplusplus
}
#endif

#endif /* W_INFO_ROW_H */
