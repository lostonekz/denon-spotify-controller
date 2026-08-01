/*
 * Transient bottom message, auto-dismiss (UI_ARCHITECTURE.md §4). Queuing
 * (depth 4, drop-newest) lives entirely in the TOPIC_TOAST subscription's
 * own core_pool_ring policy (Phase 5 plan directory comment) -- this
 * widget just shows whatever message it's told to show next; it carries no
 * queue of its own.
 */
#ifndef W_TOAST_H
#define W_TOAST_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t   *root;
    lv_obj_t   *label;
    lv_timer_t *dismiss_timer;
} w_toast_t;

/* No cfg -- nothing to configure at create time. Hidden until the first
 * w_toast_show(). */
lv_obj_t *w_toast_create(lv_obj_t *parent, w_toast_t *w);

/* Shows message immediately (replacing whatever was showing, if anything)
 * and (re)starts the auto-dismiss countdown. */
void w_toast_show(w_toast_t *w, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* W_TOAST_H */
