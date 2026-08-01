/*
 * Pager position indicator (UI_ARCHITECTURE.md §4) -- extracted from
 * ui_router.c's own Tier 4 inline implementation into a real, reusable
 * widget (Phase 5 Tier 6), same behavior, no visual change: ui_router.c
 * now calls this instead of building the dot row itself.
 */
#ifndef W_PAGE_DOTS_H
#define W_PAGE_DOTS_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define W_PAGE_DOTS_MAX 8

typedef struct {
    uint8_t count;   /* 1..W_PAGE_DOTS_MAX */
} w_page_dots_cfg_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *dots[W_PAGE_DOTS_MAX];
    uint8_t   count;
} w_page_dots_t;

/* Builds the dot row as a child of parent, aligned bottom-mid with an 8px
 * inset. NULL on a bad cfg (count == 0 or > W_PAGE_DOTS_MAX). */
lv_obj_t *w_page_dots_create(lv_obj_t *parent, w_page_dots_t *w, const w_page_dots_cfg_t *cfg);

/* Marks index active (full opacity), every other dot dimmed. No-op if
 * index >= w->count. */
void w_page_dots_set_active(w_page_dots_t *w, uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* W_PAGE_DOTS_H */
