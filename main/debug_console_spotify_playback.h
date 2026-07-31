/*
 * The `spotify play|pause|next|prev|shuffle|repeat|volume` subcommands --
 * split out of debug_console_spotify.c purely for the 400-line gate
 * (check_limits.py). Each function is called directly from cmd_spotify()'s
 * own dispatch switch there, not registered as a separate esp_console
 * command -- these are subcommands of `spotify`, matching int(int,char**)
 * only loosely (ctx is passed explicitly since esp_console's own callback
 * signature has no room for one).
 */
#ifndef DEBUG_CONSOLE_SPOTIFY_PLAYBACK_H
#define DEBUG_CONSOLE_SPOTIFY_PLAYBACK_H

#include "app_ctx.h"

int debug_console_spotify_play(app_ctx_t *ctx);
int debug_console_spotify_pause(app_ctx_t *ctx);
int debug_console_spotify_next(app_ctx_t *ctx);
int debug_console_spotify_prev(app_ctx_t *ctx);
int debug_console_spotify_shuffle(app_ctx_t *ctx, int argc, char **argv);
int debug_console_spotify_repeat(app_ctx_t *ctx, int argc, char **argv);
int debug_console_spotify_volume(app_ctx_t *ctx, int argc, char **argv);

#endif /* DEBUG_CONSOLE_SPOTIFY_PLAYBACK_H */
