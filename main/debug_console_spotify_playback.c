/*
 * See debug_console_spotify_playback.h. Split out of debug_console_spotify.c
 * for the 400-line gate.
 */
#include "debug_console_spotify_playback.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_wiring.h"
#include "spotify_intent.h"
#include "spotify_parse.h"

/* Routes through app_wiring_submit_intent() -- the same real intent-queue
 * path submit_denon_intent() uses in debug_console.c, not a shortcut around
 * it (dispatch_one_intent() in app_wiring_ticks.c routes INTENT_DOMAIN_PLAYBACK
 * into svc_spotify_submit_intent()). */
static void submit_playback_intent(app_ctx_t *ctx, playback_intent_kind_t kind,
                                     const playback_intent_payload_t *payload)
{
    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_PLAYBACK;
    intent.kind   = (uint16_t)kind;
    memcpy(intent.payload, payload, sizeof(*payload));
    if (!app_wiring_submit_intent(ctx, &intent)) {
        printf("intent queue full, dropped\n");
    }
}

int debug_console_spotify_play(app_ctx_t *ctx)
{
    playback_intent_payload_t p = { 0 };
    submit_playback_intent(ctx, PLAYBACK_INTENT_PLAY, &p);
    return 0;
}

int debug_console_spotify_pause(app_ctx_t *ctx)
{
    playback_intent_payload_t p = { 0 };
    submit_playback_intent(ctx, PLAYBACK_INTENT_PAUSE, &p);
    return 0;
}

int debug_console_spotify_next(app_ctx_t *ctx)
{
    playback_intent_payload_t p = { 0 };
    submit_playback_intent(ctx, PLAYBACK_INTENT_NEXT, &p);
    return 0;
}

int debug_console_spotify_prev(app_ctx_t *ctx)
{
    playback_intent_payload_t p = { 0 };
    submit_playback_intent(ctx, PLAYBACK_INTENT_PREVIOUS, &p);
    return 0;
}

int debug_console_spotify_shuffle(app_ctx_t *ctx, int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: spotify shuffle on|off\n");
        return 1;
    }
    playback_intent_payload_t p = { .shuffle = (strcmp(argv[2], "on") == 0) };
    submit_playback_intent(ctx, PLAYBACK_INTENT_SET_SHUFFLE, &p);
    return 0;
}

int debug_console_spotify_repeat(app_ctx_t *ctx, int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: spotify repeat off|track|context\n");
        return 1;
    }
    uint8_t mode;
    if (strcmp(argv[2], "off") == 0)          mode = SPOTIFY_REPEAT_OFF;
    else if (strcmp(argv[2], "track") == 0)   mode = SPOTIFY_REPEAT_TRACK;
    else if (strcmp(argv[2], "context") == 0) mode = SPOTIFY_REPEAT_CONTEXT;
    else {
        printf("unknown repeat mode '%s'\n", argv[2]);
        return 1;
    }
    playback_intent_payload_t p = { .repeat_mode = mode };
    submit_playback_intent(ctx, PLAYBACK_INTENT_SET_REPEAT, &p);
    return 0;
}

int debug_console_spotify_volume(app_ctx_t *ctx, int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: spotify volume <pct 0-100>\n");
        return 1;
    }
    /* Not clamped here -- spotify_intent_to_cmd() already clamps volume_pct
     * to 0-100 (spotify_intent.h), same trust-the-downstream-pure-function
     * precedent debug_console.c's own cmd_volume already relies on for
     * Denon's safety clamp. */
    playback_intent_payload_t p = { .volume_pct = (uint8_t)atoi(argv[2]) };
    submit_playback_intent(ctx, PLAYBACK_INTENT_SET_VOLUME, &p);
    return 0;
}
