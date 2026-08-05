/*
 * A QR code on a white card. Wraps LVGL's own lv_qrcode (vendored at
 * managed_components/lvgl__lvgl/src/libs/qrcode, Nayuki's qrcodegen, gated by
 * LV_USE_QRCODE -- enabled in BOTH sdkconfig.defaults and
 * test/ui_harness/lv_conf.h, since the harness has its own LVGL config).
 *
 * Three real failure modes this wrapper exists to catch, all of which are
 * silent in lv_qrcode itself:
 *
 *  1. lv_qrcode_set_size() returns void and, when its allocation fails,
 *     simply never installs the new buffer -- leaving the CONSTRUCTOR's
 *     LV_DPI_DEF (130px) one in place. You get a small, unscannable QR, not
 *     an error.
 *  2. lv_qrcode_update() computes scale = canvas_w / qr_modules with integer
 *     division. A canvas narrower than the module count gives scale 0, the
 *     copy loop never executes, and you get a BLANK WHITE CANVAS with
 *     LV_RESULT_OK and no log line at all. W_QR_MIN_SIZE_PX guards this.
 *  3. lv_qrcode_update()'s two lv_malloc()s for the qrcodegen scratch buffers
 *     use LV_ASSERT_MALLOC, which on target is a bare while(1). Those are
 *     ~111 bytes each at the versions we generate and cannot be guarded from
 *     out here -- the mitigation is that the big allocation (the canvas) is
 *     made once at create() time, so update() never needs real memory.
 *
 * The quiet zone is the white card's own padding, NOT lv_qrcode's quiet_zone
 * option: with that option off (the default) lv_qrcode centres the code in
 * the canvas with only the integer-division remainder as margin, a handful of
 * pixels. A QR sitting directly on this project's near-black background is
 * the single most common reason a rendered code won't scan.
 */
#ifndef W_QR_H
#define W_QR_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define W_QR_TEXT_MAX      64   /* the pair URL is ~33; this is not a general-purpose widget */

/* 4 px per module at the 29-module (version 3) codes this project generates,
 * which is the documented floor for a reliably scannable code. Below this,
 * failure mode 2 above starts to bite. */
#define W_QR_MIN_SIZE_PX  120

typedef struct {
    lv_obj_t *card;
    lv_obj_t *qr;
    int32_t   size_px;
    bool      ok;                    /* false -> the canvas is not the size we asked for */
    char      applied[W_QR_TEXT_MAX];
} w_qr_t;

/* Returns the card object (the thing to lay out), or NULL on failure.
 * Allocates the QR canvas once, here -- an I1 (1bpp) draw buffer, ~7.7 KB at
 * 240 px. Deliberately not deferred to first use: this board's history
 * includes allocator failures under UI-task memory pressure, and a
 * deterministic failure at ui_app_create() beats one at overlay-open time. */
lv_obj_t *w_qr_create(lv_obj_t *parent, w_qr_t *w, int32_t size_px);

/* Diff-gated -- re-encoding an unchanged string would redraw the whole
 * canvas. Returns false if the code was not rendered, in which case the
 * caller should fall back to showing the URL as text. */
bool w_qr_set_text(w_qr_t *w, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* W_QR_H */
