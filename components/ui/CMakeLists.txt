# ui -- the theme/view_model/widget/screen layer. docs/ROADMAP.md Phase 5.
#
# The ONLY component tools/check_lvgl_boundary.py's UI_COMPONENTS = {"ui"}
# exempts from the "no lv_* symbol reference outside ui" rule (docs/adr/0008).
# Every other component in this project is structurally forbidden from
# including any LVGL header at all -- enforced by that gate, not just
# convention.
#
# Subdirectories (ui_app/, ui_router/, view_model/, screens/, widgets/,
# theme/, added tier by tier) are all still component "ui" for the gate's
# purposes -- it keys off the top-level path segment under components/,
# not a nested one.
#
# REQUIRES lists exactly what's used today, same discipline svc_spotify's
# own CMakeLists.txt states explicitly -- "lvgl" lands here in Tier 2
# (idf_component.yml pins the version) even though nothing calls an lv_*
# API yet; that starts in Tier 4's ui_app.c.

idf_component_register(
    SRCS          "view_model/view_model_tone.c"
                  "view_model/view_model_format.c"
                  "view_model/view_model_text.c"
                  "view_model/view_model_scrim.c"
                  "view_model/view_model_progress.c"
                  "view_model/view_model_now_playing.c"
                  "theme/theme_tokens.c"
                  "assets/fonts/ui_font_display.c"
                  "assets/fonts/ui_font_hero.c"
                  "assets/fonts/ui_font_title.c"
                  "assets/fonts/ui_font_body.c"
                  "assets/fonts/ui_font_label.c"
                  "assets/fonts/ui_font_meta.c"
                  "view_model/view_model_now_playing_apply.c"
                  "ui_router/ui_router.c"
                  "widgets/w_page_dots.c"
                  "widgets/w_status_pill.c"
                  "widgets/w_marquee_label.c"
                  "widgets/w_progress_bar.c"
                  "widgets/w_transport.c"
                  "widgets/w_toast.c"
                  "widgets/w_art_card.c"
                  "screens/screen_stub.c"
                  "screens/screen_now_playing.c"
                  "ui_app/ui_app.c"
                  "ui_app/ui_app_watchdog.c"
    INCLUDE_DIRS  "include"
    REQUIRES      "core_types" "lvgl" "hal_display" "hal_touch" "core_state" "core_eventbus"
    PRIV_REQUIRES "esp_lcd" "esp_lcd_touch" "heap"
)

# tools/gen_assets.py's output (components/ui/assets/fonts/*.c) is generated
# with lv_font_conv's default "#include lvgl.h" guard, which is gated on
# LV_LVGL_H_INCLUDE_SIMPLE -- this managed lvgl component's own INCLUDE_DIRS
# (managed_components/lvgl__lvgl/env_support/cmake/esp.cmake) exposes
# lvgl.h at its own root, not nested under an "lvgl/" subdirectory the way
# the guard's #else branch assumes, so the SIMPLE form is the one that
# actually resolves here -- confirmed directly, not assumed. Same idiom
# LVGL's own esp.cmake uses for its similarly-named LV_CONF_INCLUDE_SIMPLE.
target_compile_definitions(${COMPONENT_LIB} PRIVATE "LV_LVGL_H_INCLUDE_SIMPLE")
