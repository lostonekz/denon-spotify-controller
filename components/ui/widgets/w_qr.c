#include "w_qr.h"

#include <string.h>

#include "theme_metrics.h"

/* The real quiet zone. QR spec wants 4 modules; at 240 px / 29 modules that
 * is 4 * 8 = 32 px, which is exactly THEME_SPACE_XL. */
#define W_QR_QUIET_PX  THEME_SPACE_XL

lv_obj_t *w_qr_create(lv_obj_t *parent, w_qr_t *w, int32_t size_px)
{
    memset(w, 0, sizeof(*w));

    if (size_px < W_QR_MIN_SIZE_PX) {
        return NULL;   /* see failure mode 2 in the header */
    }
    w->size_px = size_px;

    w->card = lv_obj_create(parent);
    if (w->card == NULL) {
        return NULL;
    }
    lv_obj_remove_flag(w->card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(w->card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(w->card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(w->card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w->card, 0, 0);
    lv_obj_set_style_radius(w->card, THEME_RADIUS_CARD, 0);
    lv_obj_set_style_pad_all(w->card, W_QR_QUIET_PX, 0);
    lv_obj_set_size(w->card, size_px + 2 * W_QR_QUIET_PX, size_px + 2 * W_QR_QUIET_PX);

    w->qr = lv_qrcode_create(w->card);
    if (w->qr == NULL) {
        return NULL;
    }
    /* Size and colours must both be set BEFORE the first lv_qrcode_update():
     * update() reads the installed draw buffer and writes the palette from
     * the stored colours, so anything set afterwards only takes effect on the
     * NEXT update. */
    lv_qrcode_set_dark_color(w->qr, lv_color_black());
    lv_qrcode_set_light_color(w->qr, lv_color_white());
    lv_qrcode_set_size(w->qr, size_px);

    /* set_size is void and fails silently, leaving the constructor's 130 px
     * buffer installed (failure mode 1). Read the buffer back rather than
     * trusting the call. */
    lv_draw_buf_t *buf = lv_canvas_get_draw_buf(w->qr);
    w->ok = buf != NULL && buf->header.w == (uint32_t)size_px;

    lv_obj_center(w->qr);
    return w->card;
}

bool w_qr_set_text(w_qr_t *w, const char *text)
{
    if (!w->ok || w->qr == NULL || text == NULL || text[0] == '\0') {
        return false;
    }
    if (strcmp(w->applied, text) == 0) {
        return true;   /* already showing this exact code */
    }

    size_t len = strlen(text);
    if (len >= sizeof(w->applied)) {
        return false;
    }

    if (lv_qrcode_update(w->qr, text, (uint32_t)len) != LV_RESULT_OK) {
        w->applied[0] = '\0';
        return false;
    }

    memcpy(w->applied, text, len + 1);
    return true;
}
