#include "screen_now_playing.h"

#include "esp_log.h"

#include "theme_colors.h"
#include "theme_fonts.h"
#include "theme_metrics.h"

/* Every widget-build step below is null-checked and propagates failure the
 * same way w_art_card_create() already did before this redesign -- real,
 * not just defensive: a genuine LVGL allocation-failure bug was found and
 * fixed on real hardware during this same redesign (see sdkconfig.defaults.
 * esp32s3's CONFIG_LV_USE_CLIB_MALLOC comment), and a NULL return here used
 * to crash instead of failing cleanly. Logged, not silent, so a future
 * regression shows up as a clear boot-time error instead of a blank screen. */
static const char *TAG = "screen_now_playing";
#define DIAG_FAIL(step) ESP_LOGE(TAG, "%s returned NULL -- LVGL object allocation failed", step)

/* Real bug, found live on real hardware (Phase 5 Tier 7 report): a fixed
 * pixel height, not LV_SIZE_CONTENT -- LV_SIZE_CONTENT means this stack's
 * own height is recomputed from its children's sizes on every relayout,
 * and now_playing_update() rewrites the progress bar's value every single
 * UI-task tick (interpolated smoothly while playing), not just when its
 * displayed text visibly changes. Combined with lv_obj_align(...,
 * LV_ALIGN_BOTTOM_MID, ...) below -- which positions this box relative to
 * its OWN height -- any per-tick recompute of that height (even a
 * sub-pixel one) shifts the whole box, and everything in it, vertically.
 * A fixed height removes the recompute dependency entirely.
 *
 * Tightened live, per feedback asking for more of the screen's top half
 * to be unobstructed artwork: title+artist ~68px + scrub-with-timestamps
 * ~36px + transport 80px + volume 56px hit band + 4 gaps (title-artist,
 * artist-scrub, scrub-transport, transport-volume) at the new tighter
 * THEME_SPACE_S (8px, was THEME_SPACE_M/16px) = ~272px of real content.
 * 276px keeps a little real margin without needing to be exact -- same
 * reasoning the previous value used, just recomputed for the new spacing.
 * Transport's 80px circle and volume's 56px hit band are real touch
 * targets, not shrunk here -- the tightening is gaps/margins only, so
 * nothing got harder to hit.
 *
 * Grew by 11px (265 -> 276) when the scrub bar's drawn track went from the
 * design spec's 3px to THEME_TRACK_H (14px), matching the volume bars --
 * this constant is NOT derived from its children, so a widget getting
 * taller silently overflows it. With LV_FLEX_ALIGN_END packing content
 * toward the bottom, an overflow pushes the title off the TOP of the box
 * rather than reporting itself, which is exactly the kind of failure that
 * looks like a layout mystery later. Recompute this whenever a child's
 * real height changes. */
#define NOW_PLAYING_CONTENT_COLUMN_HEIGHT 276

static lv_obj_t *make_content_column(lv_obj_t *parent)
{
    lv_obj_t *stack = lv_obj_create(parent);
    if (stack == NULL) {
        DIAG_FAIL("make_content_column root");
        return NULL;
    }
    lv_obj_remove_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stack, 0, 0);
    lv_obj_set_style_pad_all(stack, 0, 0);
    lv_obj_set_style_pad_row(stack, THEME_SPACE_S, 0);   /* was THEME_SPACE_M (16px) -- tightened
                                                              live, see NOW_PLAYING_CONTENT_COLUMN_
                                                              HEIGHT's own comment */
    lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_width(stack, lv_pct(100));
    lv_obj_set_height(stack, NOW_PLAYING_CONTENT_COLUMN_HEIGHT);
    lv_obj_align(stack, LV_ALIGN_BOTTOM_MID, 0, -THEME_SPACE_S);   /* was -THEME_SPACE_L (24px) --
                                                                        pulled closer to the true
                                                                        bottom edge, same request */
    lv_obj_set_style_pad_left(stack, THEME_SPACE_L, 0);
    lv_obj_set_style_pad_right(stack, THEME_SPACE_L, 0);
    return stack;
}

static lv_obj_t *now_playing_create(screen_ctx_t *ctx)
{
    screen_now_playing_ctx_t *np = (screen_now_playing_ctx_t *)ctx->user_ctx;

    lv_obj_t *root = lv_obj_create(ctx->parent);   /* NOT lv_obj_create(NULL) -- see
                                                       * screen_ctx_t.parent's own comment */
    if (root == NULL) {
        DIAG_FAIL("now_playing_create root");
        return NULL;
    }
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
        DIAG_FAIL("w_art_card_create");   /* pixel buffer allocation failed -- propagate
                                              the failure, same as any other
                                              screen-build failure */
        return NULL;
    }

    /* No header control anymore -- the power glyph and the "Bring Here"
     * tap zone were both removed per live feedback: pressing Play already
     * wakes the AVR, switches to the HEOS Music input, and resumes
     * playback there when nothing's active (w_transport.c's on_play_
     * pause_clicked(), PLAYBACK_INTENT_RESUME_ON_AVR) -- confirmed working
     * end to end on real hardware, making a separate standing "turn it on"
     * control in the top-left redundant. w_power_glyph.c/.h stay in the
     * codebase, unused, as real Phase 6 Receiver-screen infrastructure
     * (same "kept for a real planned future caller" precedent as w_status_
     * pill.c). now_playing_vm_t.device_name/show_bring_here and the intent-
     * submission plumbing this used are gone too -- see view_model_now_
     * playing.h's own comment. */
    lv_obj_t *stack = make_content_column(root);
    if (stack == NULL) {
        DIAG_FAIL("content column");
        return NULL;
    }

    w_marquee_label_cfg_t title_cfg = { .font = &ui_font_hero, .color = THEME_COLOR_TEXT_ONIMAGE, .width = lv_pct(100) };
    if (w_marquee_label_create(stack, &np->title_label, &title_cfg) == NULL) {
        DIAG_FAIL("title marquee");
        return NULL;
    }

    /* "The National at 20/400, same white and stroke [as the title]" --
     * ui_font_body is exactly 20/400 Archivo, no new font bake needed
     * (swapped from the pre-redesign ui_font_title/30-600). 2px stroke,
     * matching the title's own genuine-stroke treatment, not the 1px
     * mono/meta-class value. */
    w_stroke_label_cfg_t artist_cfg = {
        .font = &ui_font_body, .color = THEME_COLOR_TEXT_ONIMAGE,
        .width = lv_pct(100), .text_align = LV_TEXT_ALIGN_LEFT, .offset_px = 2,
    };
    if (w_stroke_label_create(stack, &np->artist_label, &artist_cfg) == NULL) {
        DIAG_FAIL("artist_label stroke");
        return NULL;
    }

    w_progress_bar_cfg_t progress_cfg = { .width = lv_pct(100), .intent_submit = np->intent_submit };
    if (w_progress_bar_create(stack, &np->progress_bar, &progress_cfg) == NULL) {
        DIAG_FAIL("progress_bar");
        return NULL;
    }

    w_transport_cfg_t transport_cfg = { .intent_submit = np->intent_submit, .intent_submit_raw = np->intent_submit_raw };
    if (w_transport_create(stack, &np->transport, &transport_cfg) == NULL) {
        DIAG_FAIL("transport");
        return NULL;
    }

    w_volume_slider_cfg_t volume_cfg = { .intent_submit = np->intent_submit_raw };
    if (w_volume_slider_create(stack, &np->volume, &volume_cfg) == NULL) {
        DIAG_FAIL("volume_slider");
        return NULL;
    }
    lv_obj_set_width(np->volume.row, lv_pct(100));

    if (w_toast_create(root, &np->toast) == NULL) {
        DIAG_FAIL("toast");
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
    w_stroke_label_set_text(&np->artist_label, u->vm.artist);

    w_progress_bar_set_progress(&np->progress_bar, u->vm.progress_permille, u->vm.elapsed, u->vm.remaining,
                                    u->vm.duration_ms);
    w_transport_set_state(&np->transport, u->vm.is_playing, u->vm.pending, u->vm.has_active_device);
    w_art_card_set_artwork(&np->art_card, u->artwork_handle, u->playback_track_id, u->artwork_track_id);

    w_volume_slider_set_value(&np->volume, u->vm.volume_half_db_raw, u->vm.volume_display);
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
