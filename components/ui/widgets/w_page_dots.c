#include "w_page_dots.h"

#define W_PAGE_DOTS_DIAMETER 12
#define W_PAGE_DOTS_GAP       10

lv_obj_t *w_page_dots_create(lv_obj_t *parent, w_page_dots_t *w, const w_page_dots_cfg_t *cfg)
{
    if (cfg->count == 0 || cfg->count > W_PAGE_DOTS_MAX) {
        return NULL;
    }

    w->root = lv_obj_create(parent);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_size(w->root, cfg->count * (W_PAGE_DOTS_DIAMETER + W_PAGE_DOTS_GAP), W_PAGE_DOTS_DIAMETER);
    lv_obj_set_flex_flow(w->root, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(w->root, W_PAGE_DOTS_GAP, 0);
    lv_obj_align(w->root, LV_ALIGN_BOTTOM_MID, 0, -8);

    w->count = cfg->count;
    for (uint8_t i = 0; i < cfg->count; i++) {
        lv_obj_t *dot = lv_obj_create(w->root);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(dot, W_PAGE_DOTS_DIAMETER, W_PAGE_DOTS_DIAMETER);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(dot, (i == 0) ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        w->dots[i] = dot;
    }

    return w->root;
}

void w_page_dots_set_active(w_page_dots_t *w, uint8_t index)
{
    if (index >= w->count) {
        return;
    }
    for (uint8_t i = 0; i < w->count; i++) {
        lv_obj_set_style_bg_opa(w->dots[i], (i == index) ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}
