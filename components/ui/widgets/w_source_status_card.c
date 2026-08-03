#include "w_source_status_card.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"

#define W_SOURCE_STATUS_CARD_ICON_BOX   24
#define W_SOURCE_STATUS_CARD_BORDER_W    2

static lv_obj_t *make_segment(lv_obj_t *parent)
{
    lv_obj_t *seg = lv_obj_create(parent);
    if (seg == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(seg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(seg, 0, 0);
    return seg;
}

static bool build_bars_icon(w_source_status_card_t *w)
{
    /* 3 vertical bars, varying height, bottom-aligned -- an abstract
     * equalizer glyph, not the real Spotify logo (this file's own header
     * comment). */
    static const uint8_t heights[3] = { 10, 18, 13 };
    lv_obj_set_flex_flow(w->icon_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w->icon_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(w->icon_container, 3, 0);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *bar = make_segment(w->icon_container);
        if (bar == NULL) {
            return false;
        }
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_size(bar, 4, heights[i]);
        w->icon_segments[i] = bar;
    }
    w->icon_segment_count = 3;
    return true;
}

static bool build_tv_icon(w_source_status_card_t *w)
{
    /* A simple screen-plus-stand silhouette -- plain lv_obj rects, no
     * custom draw callback, matching this codebase's established low-risk
     * icon idiom (w_power_glyph's ring+notch, w_page_edge_tab's chevron). */
    lv_obj_set_flex_flow(w->icon_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w->icon_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(w->icon_container, 2, 0);

    lv_obj_t *screen = make_segment(w->icon_container);
    if (screen == NULL) {
        return false;
    }
    lv_obj_set_style_radius(screen, 3, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, W_SOURCE_STATUS_CARD_BORDER_W, 0);
    lv_obj_set_size(screen, 26, 17);
    w->icon_segments[0] = screen;

    lv_obj_t *stand = make_segment(w->icon_container);
    if (stand == NULL) {
        return false;
    }
    lv_obj_set_style_radius(stand, 1, 0);
    lv_obj_set_size(stand, 10, 3);
    w->icon_segments[1] = stand;

    w->icon_segment_count = 2;
    return true;
}

lv_obj_t *w_source_status_card_create(lv_obj_t *parent, w_source_status_card_t *w,
                                          const w_source_status_card_cfg_t *cfg)
{
    w->icon_segment_count = 0;

    w->root = lv_obj_create(parent);
    if (w->root == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_CLICKABLE);   /* read-only status card -- see this
                                                               widget's own header comment */
    lv_obj_set_style_radius(w->root, THEME_RADIUS_CARD, 0);
    lv_obj_set_style_pad_all(w->root, THEME_SPACE_M, 0);
    lv_obj_set_style_pad_row(w->root, THEME_SPACE_XS, 0);
    lv_obj_set_flex_flow(w->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(w->root, 1);
    lv_obj_set_height(w->root, LV_SIZE_CONTENT);

    if (cfg->icon_kind == W_SOURCE_STATUS_CARD_ICON_NONE) {
        /* Connection screen's Telnet/HTTP cards -- no icon container at
         * all, not just an empty one, so the title sits flush at the
         * card's own top instead of leaving a blank 24px gap above it. */
        w->icon_container = NULL;
    } else {
        w->icon_container = lv_obj_create(w->root);
        if (w->icon_container == NULL) {
            return NULL;
        }
        lv_obj_remove_flag(w->icon_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(w->icon_container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(w->icon_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(w->icon_container, 0, 0);
        lv_obj_set_style_pad_all(w->icon_container, 0, 0);
        lv_obj_set_size(w->icon_container, W_SOURCE_STATUS_CARD_ICON_BOX, W_SOURCE_STATUS_CARD_ICON_BOX);

        bool icon_ok = (cfg->icon_kind == W_SOURCE_STATUS_CARD_ICON_TV)
                            ? build_tv_icon(w)
                            : build_bars_icon(w);
        if (!icon_ok) {
            return NULL;
        }
    }

    w->title_label = lv_label_create(w->root);
    if (w->title_label == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->title_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(w->title_label, &ui_font_label, 0);
    lv_label_set_text(w->title_label, cfg->title);

    w->subtitle_label = lv_label_create(w->root);
    if (w->subtitle_label == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->subtitle_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(w->subtitle_label, &ui_font_meta, 0);
    lv_obj_set_style_text_color(w->subtitle_label, THEME_COLOR_TEXT_TERTIARY, 0);

    w_source_status_card_set_state(w, false, "");

    return w->root;
}

void w_source_status_card_set_state(w_source_status_card_t *w, bool selected, const char *subtitle)
{
    lv_color_t bg_color   = selected ? THEME_COLOR_TEXT_PRIMARY : THEME_COLOR_SURFACE_HIGH;
    lv_color_t fg_color   = selected ? THEME_COLOR_SURFACE      : THEME_COLOR_TEXT_PRIMARY;

    lv_obj_set_style_bg_color(w->root, bg_color, 0);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w->root, selected ? 0 : W_SOURCE_STATUS_CARD_BORDER_W, 0);
    lv_obj_set_style_border_color(w->root, THEME_COLOR_TEXT_TERTIARY, 0);

    lv_obj_set_style_text_color(w->title_label, fg_color, 0);

    for (uint8_t i = 0; i < w->icon_segment_count; i++) {
        lv_obj_t *seg = w->icon_segments[i];
        /* TV's screen segment is an outline (transparent fill, colored
         * border); every other segment (bars, TV's stand) is a filled
         * rect -- setting both properties on every segment is harmless,
         * only the one each shape actually uses takes visible effect. */
        lv_obj_set_style_bg_color(seg, fg_color, 0);
        lv_obj_set_style_border_color(seg, fg_color, 0);
    }

    lv_label_set_text(w->subtitle_label, subtitle);
}
