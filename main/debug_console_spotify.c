/*
 * See debug_console_spotify.h. Commands: spotify status|client-id|
 * redirect-uri|pair|code|unpair|devices|set-avr-device|clear-avr-device|
 * play|pause|next|prev|shuffle|repeat|volume, plus a top-level `volume
 * <half_db>` exercising the INTENT_DOMAIN_MULTI coordinator directly.
 *
 * `spotify pair`/`spotify code` reach directly into svc_spotify_t's public
 * `state`/`session_lock` fields (pending_pair/pending_state) -- this is the
 * documented cross-task access spotify_service.h's own top comment already
 * grants the console, not a layering violation: the PKCE verifier/
 * challenge/state are generated HERE (fast, local, no network) so the
 * authorize URL can be printed synchronously; action_start_pair in
 * spotify_service_session.c is a deliberate no-op for exactly this reason.
 *
 * A static g_ctx, matching debug_console.c's own idiom (esp_console_cmd_t's
 * .func is a plain int(int,char**), no user-data argument).
 */
#include "debug_console_spotify.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"

#include "app_wiring.h"
#include "debug_console_spotify_playback.h"
#include "denon_codec.h"
#include "spotify_credentials.h"
#include "spotify_parse.h"
#include "spotify_service.h"
#include "volume_coordinator.h"

static const char *TAG = "debug_console_spotify";
static app_ctx_t   *g_ctx;

static const char *session_status_name(spotify_session_summary_t s)
{
    switch (s) {
    case SPOTIFY_SESSION_UNPAIRED:        return "UNPAIRED";
    case SPOTIFY_SESSION_PAIRING:         return "PAIRING";
    case SPOTIFY_SESSION_ACTIVE:          return "ACTIVE";
    case SPOTIFY_SESSION_REAUTH_REQUIRED: return "REAUTH_REQUIRED";
    case SPOTIFY_SESSION_REFRESHING:      return "REFRESHING";
    default:                              return "?";
    }
}

static int cmd_status(void)
{
    svc_spotify_t *s = &g_ctx->svc_spotify;

    spotify_session_summary_t status;
    bool                       reauth;
    char avr_id[SPOTIFY_AVR_DEVICE_ID_MAX];
    char avr_name[SPOTIFY_AVR_NAME_MAX];
    svc_spotify_get_status(s, &status, &reauth, avr_id, avr_name);

    printf("session=%s reauth_recommended=%s avr_pin=%s (%s)\n",
            session_status_name(status), reauth ? "yes" : "no",
            avr_id[0] != '\0' ? avr_id : "(unset)", avr_name[0] != '\0' ? avr_name : "?");

    playback_state_t pb;
    if (core_state_read(&g_ctx->state, DOMAIN_PLAYBACK, &pb, sizeof(pb))) {
        printf("playback: \"%s\" - %s (%s) is_playing=%s pos=%" PRIu32 "ms/%" PRIu32 "ms "
                "shuffle=%s repeat=%u device=\"%s\" is_avr=%d\n",
                pb.title, pb.artist, pb.album, pb.is_playing ? "yes" : "no",
                pb.position_ms, pb.duration_ms, pb.shuffle ? "yes" : "no",
                (unsigned)pb.repeat_mode, pb.device_name, (int)pb.device_is_avr);
    } else {
        printf("playback: no state yet\n");
    }
    return 0;
}

static int cmd_client_id(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: spotify client-id <id>\n");
        return 1;
    }
    if (!svc_spotify_ctrl_submit(&g_ctx->svc_spotify, SPOTIFY_CTRL_CMD_SET_CLIENT_ID, argv[2])) {
        printf("ctrl queue full, try again\n");
        return 1;
    }
    printf("client_id set\n");
    return 0;
}

static int cmd_redirect_uri(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: spotify redirect-uri <uri>\n");
        return 1;
    }
    if (!svc_spotify_ctrl_submit(&g_ctx->svc_spotify, SPOTIFY_CTRL_CMD_SET_REDIRECT_URI, argv[2])) {
        printf("ctrl queue full, try again\n");
        return 1;
    }
    printf("redirect_uri set\n");
    return 0;
}

static int cmd_pair(void)
{
    char authorize_url[SPOTIFY_AUTHORIZE_URL_MAX];
    if (!svc_spotify_begin_pair(&g_ctx->svc_spotify, authorize_url)) {
        printf("failed to begin pairing -- check client_id/redirect_uri are set "
                "(`spotify client-id <id>`, `spotify redirect-uri <uri>`)\n");
        return 1;
    }
    printf("open this URL, log in, approve access, then paste the FULL address-bar URL you\n"
            "land on (even though the page itself fails to load) into `spotify code <url>`:\n\n%s\n\n",
            authorize_url);
    return 0;
}

static int cmd_code(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: spotify code <full_pasted_redirect_url>\n");
        return 1;
    }
    if (!svc_spotify_submit_pasted_url(&g_ctx->svc_spotify, argv[2])) {
        printf("no code/state in that URL, or state mismatch -- rejecting, no exchange "
                "attempted (run `spotify pair` again for a fresh link)\n");
        return 1;
    }
    printf("code submitted, exchanging...\n");
    return 0;
}

static int cmd_unpair(void)
{
    if (!svc_spotify_ctrl_submit(&g_ctx->svc_spotify, SPOTIFY_CTRL_CMD_UNPAIR, NULL)) {
        printf("ctrl queue full, try again\n");
        return 1;
    }
    printf("unpair requested\n");
    return 0;
}

static int cmd_devices(void)
{
    svc_spotify_t *s = &g_ctx->svc_spotify;
    if (!svc_spotify_ctrl_submit(s, SPOTIFY_CTRL_CMD_REFRESH_DEVICE_LIST, NULL)) {
        printf("ctrl queue full, refresh not requested\n");
    }

    spotify_device_entry_t devices[SPOTIFY_PARSE_MAX_DEVICES];
    size_t n = svc_spotify_get_last_devices(s, devices, SPOTIFY_PARSE_MAX_DEVICES);
    if (n == 0) {
        printf("no devices cached yet -- refresh requested, run `spotify devices` again shortly\n");
        return 0;
    }
    printf("cached device list (refresh requested; re-run shortly for the freshest one):\n");
    for (size_t i = 0; i < n; i++) {
        printf("  %u: id=%s name=\"%s\" type=%s supports_volume=%s restricted=%s\n",
                (unsigned)(i + 1), devices[i].device_id, devices[i].device_name, devices[i].device_type,
                devices[i].supports_volume ? "yes" : "no", devices[i].is_restricted ? "yes" : "no");
    }
    return 0;
}

static bool is_all_digits(const char *s)
{
    if (*s == '\0') {
        return false;
    }
    for (const char *p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
}

/* Accepts either a real device_id or a small 1-based index into the most
 * recently printed `spotify devices` listing -- a device's id can itself be
 * null/absent in some restricted-session responses per Spotify's own docs,
 * so picking by position is the more robust option (Phase 4 plan §3). */
static int cmd_set_avr_device(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: spotify set-avr-device <device_id_or_index>\n");
        return 1;
    }
    svc_spotify_t *s   = &g_ctx->svc_spotify;
    const char     *arg = argv[2];

    char resolved_id[PLAYBACK_DEVICE_ID_MAX] = { 0 };
    if (is_all_digits(arg) && strlen(arg) <= 2) {
        int idx = atoi(arg);
        spotify_device_entry_t devices[SPOTIFY_PARSE_MAX_DEVICES];
        size_t n = svc_spotify_get_last_devices(s, devices, SPOTIFY_PARSE_MAX_DEVICES);
        if (idx < 1 || (size_t)idx > n) {
            printf("index %d out of range (1-%u) -- run `spotify devices` first\n", idx, (unsigned)n);
            return 1;
        }
        strncpy(resolved_id, devices[idx - 1].device_id, sizeof(resolved_id) - 1);
    } else {
        strncpy(resolved_id, arg, sizeof(resolved_id) - 1);
    }

    if (!svc_spotify_ctrl_submit(s, SPOTIFY_CTRL_CMD_SET_AVR_DEVICE, resolved_id)) {
        printf("ctrl queue full, try again\n");
        return 1;
    }
    printf("AVR device pin set to id=%s\n", resolved_id);
    return 0;
}

static int cmd_clear_avr_device(void)
{
    if (!svc_spotify_ctrl_submit(&g_ctx->svc_spotify, SPOTIFY_CTRL_CMD_CLEAR_AVR_DEVICE, NULL)) {
        printf("ctrl queue full, try again\n");
        return 1;
    }
    printf("AVR device pin cleared\n");
    return 0;
}

static int cmd_spotify(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: spotify status|client-id <id>|redirect-uri <uri>|pair|code <url>|unpair|"
                "devices|set-avr-device <id|index>|clear-avr-device|play|pause|next|prev|"
                "shuffle on/off|repeat off/track/context|volume <pct>\n");
        return 1;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "status") == 0)           return cmd_status();
    if (strcmp(sub, "client-id") == 0)        return cmd_client_id(argc, argv);
    if (strcmp(sub, "redirect-uri") == 0)     return cmd_redirect_uri(argc, argv);
    if (strcmp(sub, "pair") == 0)             return cmd_pair();
    if (strcmp(sub, "code") == 0)              return cmd_code(argc, argv);
    if (strcmp(sub, "unpair") == 0)            return cmd_unpair();
    if (strcmp(sub, "devices") == 0)           return cmd_devices();
    if (strcmp(sub, "set-avr-device") == 0)    return cmd_set_avr_device(argc, argv);
    if (strcmp(sub, "clear-avr-device") == 0)  return cmd_clear_avr_device();
    if (strcmp(sub, "play") == 0)               return debug_console_spotify_play(g_ctx);
    if (strcmp(sub, "pause") == 0)               return debug_console_spotify_pause(g_ctx);
    if (strcmp(sub, "next") == 0)                 return debug_console_spotify_next(g_ctx);
    if (strcmp(sub, "prev") == 0)                  return debug_console_spotify_prev(g_ctx);
    if (strcmp(sub, "shuffle") == 0)                return debug_console_spotify_shuffle(g_ctx, argc, argv);
    if (strcmp(sub, "repeat") == 0)                  return debug_console_spotify_repeat(g_ctx, argc, argv);
    if (strcmp(sub, "volume") == 0)                   return debug_console_spotify_volume(g_ctx, argc, argv);

    printf("unknown subcommand '%s'\n", sub);
    return 1;
}

/* Not under `denon`/`spotify` -- exercises adr/0007's cross-domain
 * coordinator directly (Phase 4 plan §7). `denon volume`/`spotify volume`
 * still exist unchanged, targeting their own domain explicitly. */
static int cmd_volume_multi(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: volume <half_db 0-%u>\n", DENON_MV_HALF_DB_MAX);
        return 1;
    }
    multi_intent_payload_t p = { .volume_half_db = (uint16_t)atoi(argv[1]) };
    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_MULTI;
    intent.kind   = (uint16_t)MULTI_INTENT_SET_VOLUME;
    memcpy(intent.payload, &p, sizeof(p));
    if (!app_wiring_submit_intent(g_ctx, &intent)) {
        printf("intent queue full, dropped\n");
    }
    return 0;
}

bool debug_console_spotify_register(app_ctx_t *ctx)
{
    g_ctx = ctx;

    const esp_console_cmd_t spotify_cmd = {
        .command = "spotify",
        .help    = "status|client-id <id>|redirect-uri <uri>|pair|code <url>|unpair|devices|"
                    "set-avr-device <id|index>|clear-avr-device|play|pause|next|prev|"
                    "shuffle on/off|repeat off/track/context|volume <pct>",
        .hint    = NULL,
        .func    = &cmd_spotify,
    };
    if (esp_console_cmd_register(&spotify_cmd) != ESP_OK) {
        ESP_LOGE(TAG, "esp_console_cmd_register(spotify) failed");
        return false;
    }

    const esp_console_cmd_t volume_cmd = {
        .command = "volume",
        .help    = "<half_db> -- retargets to Denon or the active Spotify Connect device (adr/0007)",
        .hint    = NULL,
        .func    = &cmd_volume_multi,
    };
    if (esp_console_cmd_register(&volume_cmd) != ESP_OK) {
        ESP_LOGE(TAG, "esp_console_cmd_register(volume) failed");
        return false;
    }

    return true;
}
