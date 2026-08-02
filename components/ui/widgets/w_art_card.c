#include "w_art_card.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#include "theme_colors.h"
#include "view_model_scrim.h"

#define W_ART_CARD_SCRIM_BASELINE_OPA 38   /* ~0.15*255 -- initial/no-artwork-yet baseline.
                                               A real, user-driven finding from Phase 5 Tier 7
                                               real-hardware testing, not the original design:
                                               the plan's own Art-forward mock called for a
                                               vertical gradient scrim (0.15->0.92 opacity), but
                                               RGB565's 16-bit color depth made that smooth
                                               opacity ramp visibly band/posterize on real
                                               hardware, confirmed on a real photo of the panel.
                                               This is now a flat, solid opacity (still per-track
                                               adaptive via view_model_scrim_opacity_for_luma(),
                                               just applied uniformly instead of only at a
                                               gradient's bottom stop) -- no gradient anywhere on
                                               this widget, by design, not an oversight. */

#define W_ART_CARD_SCRIM_HEIGHT_PCT 46     /* Real bug, found live (Tier 7 UI polish pass):
                                               the flat-opacity fix above killed the gradient's
                                               visible banding but left the scrim rect itself at
                                               the gradient's own full LV_PCT(100) size -- so
                                               view_model_scrim.h's 0.92 BASELINE_OPA (the value
                                               meant for a gradient's BOTTOM stop only) covered
                                               the entire card at near-full opacity for any cover
                                               at or below the luma midpoint (VIEW_MODEL_SCRIM_
                                               LUMA_MIDPOINT's own comment: "most covers are
                                               darker than this"), i.e. almost every real track --
                                               the artwork was barely visible behind it. Fix:
                                               shrink the scrim to just the bottom band where the
                                               overlay text/controls actually sit (the bottom
                                               stack's own real content -- hero title, artist,
                                               progress bar, transport row -- measures well under
                                               half the 480px card), bottom-aligned, leaving the
                                               artwork's upper majority unobstructed. Restores the
                                               original mock's intent (mostly-visible art, heavy
                                               darkening only right behind the text) without
                                               reintroducing a gradient. */

static uint16_t *alloc_pixel_buf(void)
{
#ifdef ESP_PLATFORM
    return (uint16_t *)heap_caps_malloc(ARTWORK_PIXEL_BYTES, MALLOC_CAP_SPIRAM);
#else
    return (uint16_t *)malloc(ARTWORK_PIXEL_BYTES);   /* test/ui_harness: no heap_caps on host */
#endif
}

/* Coarse grid over the bottom ~35% band (plan §5), not every pixel --
 * ARTWORK_PIXEL_HEIGHT/WIDTH are 320 each, so even this step samples
 * ~14*40 = 560 pixels, plenty for an average that only feeds a scrim
 * opacity decision, not a precise histogram. */
static uint8_t sample_bottom_band_luma(const uint16_t *buf)
{
    const int32_t band_start_y = (ARTWORK_PIXEL_HEIGHT * 65) / 100;
    const int32_t step = 8;
    uint32_t sum = 0;
    uint32_t count = 0;
    for (int32_t y = band_start_y; y < ARTWORK_PIXEL_HEIGHT; y += step) {
        for (int32_t x = 0; x < ARTWORK_PIXEL_WIDTH; x += step) {
            uint16_t px = buf[y * ARTWORK_PIXEL_WIDTH + x];
            uint8_t  r8 = (uint8_t)(((px >> 11) & 0x1Fu) * 255u / 31u);
            uint8_t  g8 = (uint8_t)(((px >> 5) & 0x3Fu) * 255u / 63u);
            uint8_t  b8 = (uint8_t)((px & 0x1Fu) * 255u / 31u);
            sum   += ((uint32_t)r8 * 77u + (uint32_t)g8 * 151u + (uint32_t)b8 * 28u) >> 8;
            count += 1u;
        }
    }
    return (count != 0u) ? (uint8_t)(sum / count) : 0u;
}

lv_obj_t *w_art_card_create(lv_obj_t *parent, w_art_card_t *w, const w_art_card_cfg_t *cfg)
{
    w->pixel_buf = alloc_pixel_buf();
    if (w->pixel_buf == NULL) {
        return NULL;
    }
    w->artwork_pixels    = cfg->artwork_pixels;
    w->current_handle    = (artwork_handle_t){ 0, 0 };
    w->current_track_id[0] = '\0';

    w->root = lv_obj_create(parent);
    lv_obj_remove_flag(w->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(w->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->root, 0, 0);
    lv_obj_set_style_pad_all(w->root, 0, 0);
    /* No radius, no clip_corner -- a real finding from Phase 5 Tier 7
     * real-hardware testing, not an oversight. clip_corner was the original
     * design (24px rounded corners per the Art-forward mock), but with it
     * on, w->root's three children (placeholder/image/scrim) force LVGL
     * into its compositing-layer path (lv_draw_layer_alloc_buf ->
     * lv_draw_buf_create -> lv_tlsf_malloc), and on this exact real-hardware
     * config that call never returns -- confirmed stuck deep inside
     * lv_tlsf.c's own block search (block_next/block_mark_as_used), not
     * failing/asserting, at every pool size tried (512 KiB, 2 MiB), so this
     * is not a memory-budget problem. Root cause not yet found
     * (LV_USE_DRAW_SW_COMPLEX is already on, ruling out the obvious
     * missing-feature-flag explanation). Removing clip_corner confirmed
     * fixes the hang. A second real finding, from a later real-hardware
     * round: leaving radius set on w->root with clip_corner off does
     * nothing useful (children are square and unclipped, so nothing is
     * actually rounded) but was left in anyway -- and real hardware showed
     * a small stray artifact at each of the 4 corners, consistent with
     * LVGL's software rasterizer still computing a rounded-rect edge for a
     * fully transparent fill. Removed entirely, not just left at 0, so
     * there's no rounded-rect edge computation happening here at all. The
     * real user-visible cost of all this: the art card's corners are
     * square, not the design's intended 24px rounded -- filed as a real
     * follow-up (either root-cause the layer-alloc hang, or move corner-
     * rounding into the pixel buffer itself before handing it to LVGL), not
     * silently accepted. */
    lv_obj_set_size(w->root, LV_PCT(100), LV_PCT(100));

    w->placeholder = lv_obj_create(w->root);
    lv_obj_remove_flag(w->placeholder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(w->placeholder, THEME_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(w->placeholder, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w->placeholder, 0, 0);   /* see w->root's own comment above --
                                                          LVGL's default theme radius, uncleared */
    lv_obj_set_style_border_width(w->placeholder, 0, 0);
    lv_obj_set_size(w->placeholder, LV_PCT(100), LV_PCT(100));

    w->img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    w->img_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    w->img_dsc.header.w       = ARTWORK_PIXEL_WIDTH;
    w->img_dsc.header.h       = ARTWORK_PIXEL_HEIGHT;
    w->img_dsc.header.stride = ARTWORK_PIXEL_WIDTH * 2u;
    w->img_dsc.data_size       = ARTWORK_PIXEL_BYTES;
    w->img_dsc.data              = (const uint8_t *)w->pixel_buf;

    w->image = lv_image_create(w->root);
    lv_image_set_src(w->image, &w->img_dsc);
    lv_obj_set_size(w->image, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(w->image, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_style_opa(w->image, LV_OPA_TRANSP, 0);   /* placeholder showing until the
                                                            first real artwork arrives */

    /* The scrim IS the backdrop's darkening -- a flat, solid-opacity black
     * rect on top of the image (no gradient -- see this file's top comment),
     * adaptive opacity per plan §5 (applied per-track in
     * w_art_card_set_artwork()). */
    w->scrim = lv_obj_create(w->root);
    lv_obj_remove_flag(w->scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(w->scrim, 0, 0);   /* see w->root's own comment above --
                                                    LVGL's default theme radius, uncleared */
    lv_obj_set_style_border_width(w->scrim, 0, 0);
    lv_obj_set_style_bg_color(w->scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(w->scrim, W_ART_CARD_SCRIM_BASELINE_OPA, 0);
    lv_obj_set_size(w->scrim, LV_PCT(100), LV_PCT(W_ART_CARD_SCRIM_HEIGHT_PCT));
    lv_obj_align(w->scrim, LV_ALIGN_BOTTOM_MID, 0, 0);

    return w->root;
}

void w_art_card_set_artwork(w_art_card_t *w, artwork_handle_t handle,
                              const char *playback_track_id, const char *artwork_track_id)
{
    if (strncmp(w->current_track_id, playback_track_id, sizeof(w->current_track_id)) != 0) {
        strncpy(w->current_track_id, playback_track_id, sizeof(w->current_track_id) - 1);
        w->current_track_id[sizeof(w->current_track_id) - 1] = '\0';
        w->current_handle = (artwork_handle_t){ 0, 0 };
        lv_obj_set_style_opa(w->image, LV_OPA_TRANSP, 0);   /* back to placeholder immediately */
    }

    /* artwork_track_id lags behind playback_track_id until a real decode
     * completes for the CURRENTLY playing track -- see this function's own
     * header comment. A mismatch here just means "no artwork for the
     * current track yet (or ever, on a permanent decode failure)," so stay
     * on the placeholder rather than render a handle that's actually still
     * the previous track's. */
    if (strncmp(playback_track_id, artwork_track_id, sizeof(w->current_track_id)) != 0) {
        return;
    }

    bool handle_is_empty = (handle.slot == 0u && handle.generation == 0u);
    bool handle_changed    = (handle.slot != w->current_handle.slot ||
                                handle.generation != w->current_handle.generation);
    if (handle_is_empty || !handle_changed) {
        return;
    }

    if (w->artwork_pixels.lock_copy == NULL ||
        !w->artwork_pixels.lock_copy(w->artwork_pixels.ctx, handle, w->pixel_buf, ARTWORK_PIXEL_BYTES)) {
        return;   /* stale/invalid handle, or no provider wired -- stays on placeholder */
    }
    /* Tier 7 live debugging measured this copy at ~8ms -- not the real
     * cost, see the render-cost comment below (and ui_app_watchdog.c's
     * own threshold comment) for what actually is. */
    w->current_handle = handle;

    uint8_t  luma       = sample_bottom_band_luma(w->pixel_buf);
    lv_opa_t scrim_opa = view_model_scrim_opacity_for_luma(luma);
    lv_obj_set_style_bg_opa(w->scrim, scrim_opa, 0);

    /* Real bug, found live (Tier 7): animating w->image's own opa from
     * TRANSP->COVER (lv_anim_set_exec_cb(anim_set_image_opa)) forces
     * LVGL's software renderer through a real alpha-compositing layer for
     * every animated frame of a near-full-screen image -- the exact same
     * class of expensive layer-allocation path as the clip_corner hang
     * found earlier this same tier (both are LVGL layer-compositing costs
     * this project has now hit twice). Measured live: the FIRST frame of
     * this exact animation (the only one ever exercised on real hardware
     * -- every earlier attempt failed before a real image ever reached
     * this code) took 337ms in a single ui_app_task loop iteration, well
     * over R10's 100ms stall assertion and its own "never blocks" rule.
     * Fixed by setting full opacity directly instead of animating it --
     * a plain opaque image blit doesn't need the alpha-compositing layer
     * at all. Real cost: the smooth 600ms crossfade is gone, replaced by
     * an instant pop once the pixel copy completes -- the scrim's own
     * opacity is still applied instantly above, so this isn't a new
     * asymmetry, just a lost polish detail. Filed as a real follow-up
     * (either root-cause LVGL's layer-alloc cost directly, or fade via a
     * cheaper mechanism that doesn't force a layer), not silently
     * accepted -- same treatment this file's own top comment already
     * gives the clip_corner/square-corners finding. */
    lv_obj_set_style_opa(w->image, LV_OPA_COVER, 0);
    lv_obj_invalidate(w->image);   /* pixel_buf changed under the same img_dsc.data pointer */
}
