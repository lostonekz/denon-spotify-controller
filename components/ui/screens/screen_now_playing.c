#include "screen_now_playing.h"

#include <stdio.h>
#include <time.h>

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
    lv_obj_set_height(stack, LV_SIZE_CONTENT);
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

    np->clock_label = lv_label_create(root);
    lv_obj_set_style_text_font(np->clock_label, &ui_font_meta, 0);
    lv_obj_set_style_text_color(np->clock_label, THEME_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(np->clock_label, LV_ALIGN_TOP_RIGHT, -THEME_SPACE_L, THEME_SPACE_L);

    /* No room name -- UI_ARCHITECTURE.md §6's Art-forward top overlay calls
     * for "room name, mono/uppercase + live clock," but no field for it
     * exists anywhere in core_state (Settings, Phase 6, is the only real
     * candidate owner). Shipping the clock alone rather than fabricating a
     * fake setting; a documented Tier 6 scope reduction, not a silent
     * omission -- see the plan file's own Tier 6 findings. */

    if (w_status_pill_create(root, &np->status_pill) == NULL) {
        return NULL;
    }
    lv_obj_align(np->status_pill.root, LV_ALIGN_TOP_LEFT, THEME_SPACE_L, THEME_SPACE_L);

    lv_obj_t *stack = make_bottom_stack(root);

    w_marquee_label_cfg_t title_cfg = { .font = &ui_font_hero, .color = THEME_COLOR_TEXT_PRIMARY, .width = LV_PCT(100) };
    w_marquee_label_create(stack, &np->title_label, &title_cfg);

    np->artist_label = lv_label_create(stack);
    lv_obj_set_style_text_font(np->artist_label, &ui_font_body, 0);
    lv_obj_set_style_text_color(np->artist_label, THEME_COLOR_TEXT_SECONDARY, 0);

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
    w_art_card_set_artwork(&np->art_card, u->artwork_handle, u->artwork_track_id);

    lv_label_set_text(np->volume_label, u->vm.volume_display);
    lv_obj_set_style_text_color(np->volume_label,
                                  (u->vm.volume_tone == TONE_LIVE) ? THEME_COLOR_TEXT_SECONDARY : THEME_COLOR_TEXT_TERTIARY, 0);

    /* Live wall-clock, HH:MM -- plain libc time(), not a forbidden svc/app
     * call (net_time.c already keeps the system clock SNTP-synced; this
     * screen just reads it, same as any other C code on the target is free
     * to). No net_time dependency, no layering exception needed.
     *
     * localtime(), not localtime_r(): the reentrant form isn't declared by
     * this host toolchain's UCRT headers at all (a real build error hit
     * here -- UCRT has its own non-POSIX localtime_s() instead, reversed
     * argument order, ESP-IDF's newlib has neither's ABI in common with
     * the other). Plain localtime()'s internal static buffer is safe here:
     * the UI task is single-threaded and the result is copied out via
     * snprintf() immediately, before anything else could run. */
    time_t     now    = time(NULL);
    struct tm *tm_now = localtime(&now);
    char clock_buf[6];
    if (tm_now != NULL && snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d", tm_now->tm_hour, tm_now->tm_min) > 0) {
        lv_label_set_text(np->clock_label, clock_buf);
    }
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
