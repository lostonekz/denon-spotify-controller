#include "screen_now_playing.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"
#include "view_model_text.h"

/* now_playing_vm_t.accent_rgb565 is a packed RGB565 uint16_t (theme_tokens.h's
 * own convention: R bits 15..11, G bits 10..5, B bits 4..0) -- LVGL 9's
 * lv_color_t is always a plain {blue,green,red} byte-triple struct
 * regardless of LV_COLOR_DEPTH (confirmed directly against lv_color.h;
 * LV_COLOR_DEPTH only affects the display buffer's own storage format),
 * so this needs an explicit unpack, not a reinterpret-cast. */
static lv_color_t rgb565_to_lv_color(uint16_t rgb565)
{
    uint8_t r5 = (uint8_t)((rgb565 >> 11) & 0x1Fu);
    uint8_t g6 = (uint8_t)((rgb565 >> 5) & 0x3Fu);
    uint8_t b5 = (uint8_t)(rgb565 & 0x1Fu);
    return lv_color_make((uint8_t)(r5 * 255u / 31u), (uint8_t)(g6 * 255u / 63u), (uint8_t)(b5 * 255u / 31u));
}

/* Real bug, found live (real-hardware report): a fixed pixel height, not
 * LV_SIZE_CONTENT -- LV_SIZE_CONTENT means this stack's own height is
 * recomputed from its children's sizes on every relayout, and
 * now_playing_update() rewrites the progress bar's value and elapsed/
 * remaining time labels every single UI-task tick (interpolated smoothly
 * while playing), not just when their displayed text visibly changes.
 * Combined with lv_obj_align(..., LV_ALIGN_BOTTOM_MID, ...) below -- which
 * positions this box relative to its OWN height -- any per-tick recompute
 * of that height (even a sub-pixel one) shifts the whole box, and
 * everything in it, vertically. This matched the reported symptom exactly:
 * the art card (touched only on a real track change, per w_art_card.c's own
 * no-op-unless-changed contract) stayed rock stable, while this stack's
 * children (updated every tick) visibly jumped to a different vertical
 * anchor each frame. A fixed height removes the recompute dependency
 * entirely; LV_FLEX_ALIGN_END on the main axis (below, unchanged) still
 * packs real content against the BOTTOM of this box exactly as before, so
 * normal appearance is unaffected as long as the fixed height comfortably
 * exceeds real content height (it does, with real margin -- see the
 * constant's own sizing below). */
#define NOW_PLAYING_BOTTOM_STACK_HEIGHT 240   /* hero title (~34px) + gap + artist
                                                  (~30px) + gap + progress block
                                                  (bar + time row, ~24px) + gap +
                                                  transport row (THEME_TOUCH_MIN,
                                                  56px) + gaps (3x THEME_SPACE_S)
                                                  is ~170px of real content;
                                                  240px leaves real margin for
                                                  font ascent/descent and any
                                                  future content growth without
                                                  ever needing to be exact */

static lv_obj_t *make_bottom_stack(lv_obj_t *parent)
{
    lv_obj_t *stack = lv_obj_create(parent);
    lv_obj_remove_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stack, 0, 0);
    lv_obj_set_style_pad_all(stack, 0, 0);
    lv_obj_set_style_pad_row(stack, THEME_SPACE_S, 0);
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_width(stack, LV_PCT(88));
    lv_obj_set_height(stack, NOW_PLAYING_BOTTOM_STACK_HEIGHT);
    lv_obj_align(stack, LV_ALIGN_BOTTOM_MID, 0, -THEME_SPACE_L);
    return stack;
}

static lv_obj_t *now_playing_create(screen_ctx_t *ctx)
{
    screen_now_playing_ctx_t *np = (screen_now_playing_ctx_t *)ctx->user_ctx;

    lv_obj_t *root = lv_obj_create(ctx->parent);   /* NOT lv_obj_create(NULL) -- see
                                                       * screen_ctx_t.parent's own comment */
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, THEME_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    /* A real finding from real-hardware testing (Phase 5 Tier 7), same root
     * cause as ui_router.c's own top comment: LVGL's default theme
     * (CONFIG_LV_USE_THEME_DEFAULT=y) gives every plain lv_obj_create()
     * object a small default radius unless explicitly cleared -- this is a
     * full-screen (480x480) object, so an un-cleared themed radius leaves
     * this opaque background not quite reaching the true 4 corners of the
     * display, exposing whatever's underneath (router->pager's own
     * background) at exactly those corners. */
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    w_art_card_cfg_t art_cfg = { .artwork_pixels = np->artwork_pixels };
    if (w_art_card_create(root, &np->art_card, &art_cfg) == NULL) {
        return NULL;   /* pixel buffer allocation failed -- propagate the
                           failure, same as any other screen-build failure */
    }

    /* Top-right overlay slot: originally a live clock (Tier 6). Replaced
     * (user request, Tier 7 UI polish pass) with the active Spotify Connect
     * device name -- closer to the design system's own original intent for
     * this slot ("room name, mono/uppercase + live clock") than a clock
     * alone ever was, since "which device is currently playing" is real
     * data core_state already has (playback_state_t.device_name) where no
     * room-name field exists anywhere. Read-only display -- a tap-to-switch
     * device picker was built and confirmed working, then explicitly
     * descoped by the user; kept as a plain, non-interactive label. */
    np->device_label = lv_label_create(root);
    lv_obj_set_style_text_font(np->device_label, &ui_font_meta, 0);
    lv_obj_set_style_text_color(np->device_label, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(np->device_label, LV_ALIGN_TOP_RIGHT, -THEME_SPACE_L, THEME_SPACE_L);

    if (w_status_pill_create(root, &np->status_pill) == NULL) {
        return NULL;
    }
    lv_obj_align(np->status_pill.root, LV_ALIGN_TOP_LEFT, THEME_SPACE_L, THEME_SPACE_L);

    lv_obj_t *stack = make_bottom_stack(root);

    w_marquee_label_cfg_t title_cfg = { .font = &ui_font_hero, .color = THEME_COLOR_TEXT_PRIMARY, .width = LV_PCT(100) };
    w_marquee_label_create(stack, &np->title_label, &title_cfg);

    np->artist_label = lv_label_create(stack);
    /* Same family as the hero title (ui_font_title is Archivo 600, same
     * family as ui_font_hero's Archivo 700 -- just a smaller size/lighter
     * weight, per user request). Explicit width + LEFT align, matching
     * title_cfg's own width=LV_PCT(100): the stack's shared flex cross-axis
     * align is CENTER (make_bottom_stack()), which only affects children
     * that DON'T span the full stack width -- giving artist_label the same
     * full width as the title takes it out of that centering the same way,
     * without touching the stack's shared alignment (which progress_bar/
     * transport still rely on being CENTER-safe-for-100%-width). */
    lv_obj_set_style_text_font(np->artist_label, &ui_font_title, 0);
    lv_obj_set_style_text_color(np->artist_label, THEME_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_align(np->artist_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(np->artist_label, LV_PCT(100));

    w_progress_bar_cfg_t progress_cfg = { .width = LV_PCT(100) };
    w_progress_bar_create(stack, &np->progress_bar, &progress_cfg);

    w_transport_cfg_t transport_cfg = { .intent_submit = np->intent_submit };
    w_transport_create(stack, &np->transport, &transport_cfg);

    np->volume_label = lv_label_create(root);
    lv_obj_set_style_text_font(np->volume_label, &ui_font_meta, 0);
    lv_obj_set_style_text_color(np->volume_label, THEME_COLOR_TEXT_TERTIARY, 0);
    lv_obj_align(np->volume_label, LV_ALIGN_BOTTOM_RIGHT, -THEME_SPACE_L, -(THEME_SPACE_L + THEME_TOUCH_MIN / 2 - 8));

    if (w_toast_create(root, &np->toast) == NULL) {
        return NULL;
    }

    return root;
}

static void now_playing_mount(screen_ctx_t *ctx) { (void)ctx; }
static void now_playing_unmount(screen_ctx_t *ctx) { (void)ctx; }
static void now_playing_destroy(screen_ctx_t *ctx) { (void)ctx; }

static void now_playing_update(screen_ctx_t *ctx, const void *vm)
{
    screen_now_playing_ctx_t          *np = (screen_now_playing_ctx_t *)ctx->user_ctx;
    const screen_now_playing_update_t *u  = (const screen_now_playing_update_t *)vm;

    w_marquee_label_set_text(&np->title_label, u->vm.title);
    lv_label_set_text(np->artist_label, u->vm.artist);

    lv_color_t accent = rgb565_to_lv_color(u->vm.accent_rgb565);
    w_progress_bar_set_progress(&np->progress_bar, u->vm.progress_permille, u->vm.elapsed, u->vm.remaining, accent);
    w_transport_set_state(&np->transport, u->vm.is_playing, u->vm.pending);
    w_status_pill_set_tone(&np->status_pill, u->vm.tone);
    w_art_card_set_artwork(&np->art_card, u->artwork_handle, u->playback_track_id, u->artwork_track_id);

    lv_label_set_text(np->volume_label, u->vm.volume_display);
    lv_obj_set_style_text_color(np->volume_label,
                                  (u->vm.volume_tone == TONE_LIVE) ? THEME_COLOR_TEXT_SECONDARY : THEME_COLOR_TEXT_TERTIARY, 0);

    lv_label_set_text(np->device_label, u->vm.device_name);
}

screen_if_t screen_now_playing_get_interface(void)
{
    screen_if_t iface = {
        .create   = now_playing_create,
        .mount    = now_playing_mount,
        .update    = now_playing_update,
        .unmount  = now_playing_unmount,
        .destroy  = now_playing_destroy,
    };
    return iface;
}
