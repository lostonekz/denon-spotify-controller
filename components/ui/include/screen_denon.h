/*
 * The Receiver screen -- composition only, same discipline screen_now_
 * playing.c's own top comment states: no formatting, no business rules,
 * just widget setters fed from denon_vm_t. Simpler than Now Playing: a
 * plain designed dark background, never unpredictable album art, so no
 * stroke-label/marquee/scrim machinery is needed anywhere here, and
 * denon_vm_t (view_model_denon.h) is used directly as the update() payload
 * -- no wider wrapper struct like screen_now_playing_update_t, since there's
 * no artwork handle/track-id complexity on this screen.
 */
#ifndef SCREEN_DENON_H
#define SCREEN_DENON_H

#include "intent_submit_types.h"
#include "ui_router.h"
#include "w_power_row.h"
#include "w_source_status_card.h"
#include "w_volume_slider.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Set by the caller (ui_app.c) before ui_router_register_screen();
     * create() reads this once and never again. The raw/unwrapped provider
     * -- every control on this screen submits Denon-domain intents with a
     * near-instant telnet-echoed round trip, the same reasoning
     * w_volume_slider/w_power_glyph already established elsewhere, so no
     * wrapped/pending-tracked provider is needed here at all. */
    intent_submit_provider_t intent_submit_raw;

    /* Owned by this screen from create() onward. */
    w_power_row_t          power_row;
    w_source_status_card_t spotify_card;
    w_source_status_card_t tv_card;
    w_volume_slider_t      volume;
} screen_denon_ctx_t;

screen_if_t screen_denon_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_DENON_H */
