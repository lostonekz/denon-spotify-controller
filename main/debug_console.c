/*
 * See debug_console.h. Commands: denon status|host|power|volume|mute|
 * input|mode|fuzz. `fuzz` routes through app_wiring_submit_intent() --
 * the same real intent-queue-to-svc_denon path everything else uses, per
 * docs/ROADMAP.md Phase 3's own exit criterion ("the safety clamp holds
 * under a fuzzed intent stream"), not a shortcut around it.
 *
 * A static g_ctx, not a per-command user-data pointer: esp_console_cmd_t's
 * .func is a plain int(int,char**), no argument for one -- the standard
 * idiom for every ESP-IDF console example that needs shared state.
 */
#include "debug_console.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "esp_random.h"

#include "app_wiring.h"
#include "denon_codec.h"
#include "denon_codec_tables.h"
#include "denon_intent.h"

#ifdef CONFIG_DSC_SPOTIFY_DEBUG_CONSOLE
#include "debug_console_spotify.h"
#endif

static const char *TAG = "debug_console";
static app_ctx_t   *g_ctx;

static void submit_denon_intent(denon_intent_kind_t kind, const denon_intent_payload_t *payload)
{
    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_DENON;
    intent.kind   = (uint16_t)kind;
    memcpy(intent.payload, payload, sizeof(*payload));
    if (!app_wiring_submit_intent(g_ctx, &intent)) {
        printf("intent queue full, dropped\n");
    }
}

static int cmd_status(void)
{
    denon_state_t s;
    if (!core_state_read(&g_ctx->state, DOMAIN_DENON, &s, sizeof(s))) {
        printf("no state yet\n");
        return 0;
    }
    char vol_display[8];
    denon_mv_to_display(s.volume_half_db, vol_display, sizeof(vol_display));
    printf("link=%d status=%d power=%s volume=%sdB mute=%s input_id=%u mode_id=%u version=%" PRIu32 "\n",
            (int)svc_denon_link_state(&g_ctx->svc_denon), (int)s.meta.status,
            s.power_on ? "on" : "off", vol_display, s.muted ? "on" : "off",
            s.input_id, s.listening_mode_id, s.meta.version);
    return 0;
}

static int cmd_host(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: denon host <ip>\n");
        return 1;
    }
    if (!settings_set_str(&g_ctx->settings, SETTING_DENON_HOST, argv[2])) {
        printf("failed to set denon_host\n");
        return 1;
    }
    printf("denon_host set to %s (takes effect within ~1s)\n", argv[2]);
    return 0;
}

static int cmd_power(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: denon power on|off\n");
        return 1;
    }
    denon_intent_payload_t p = { .power_on = (strcmp(argv[2], "on") == 0) };
    submit_denon_intent(DENON_INTENT_SET_POWER, &p);
    return 0;
}

static int cmd_volume(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: denon volume <half_db 0-%u>\n", DENON_MV_HALF_DB_MAX);
        return 1;
    }
    denon_intent_payload_t p = { .volume_half_db = (uint16_t)atoi(argv[2]) };
    submit_denon_intent(DENON_INTENT_SET_VOLUME, &p);
    return 0;
}

static int cmd_mute(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: denon mute on|off\n");
        return 1;
    }
    denon_intent_payload_t p = { .muted = (strcmp(argv[2], "on") == 0) };
    submit_denon_intent(DENON_INTENT_SET_MUTE, &p);
    return 0;
}

static int cmd_input(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: denon input <name, e.g. cd/phono/bd/tv/game/net/bt>\n");
        return 1;
    }
    char probe[40] = "SI";
    size_t n = strlen(argv[2]);
    if (n >= sizeof(probe) - 2) {
        printf("name too long\n");
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        probe[2 + i] = (char)toupper((unsigned char)argv[2][i]);
    }
    probe[2 + n] = '\0';

    denon_input_id_t id;
    if (denon_si_parse(probe, &id) != DENON_TABLE_OK) {
        printf("unknown input '%s'\n", argv[2]);
        return 1;
    }
    denon_intent_payload_t p = { .input_id = (uint8_t)id };
    submit_denon_intent(DENON_INTENT_SET_INPUT, &p);
    return 0;
}

/* Rejoins argv[2..] with spaces before probing -- MS's wire codes can
 * contain a literal space (e.g. "PURE DIRECT"), and esp_console splits
 * the typed line on whitespace before this handler ever sees it. */
static int cmd_mode(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: denon mode <name..., e.g. movie/music/direct/pure direct/stereo>\n");
        return 1;
    }
    char probe[40] = "MS";
    size_t pos = 2;
    for (int i = 2; i < argc; i++) {
        if (i > 2) {
            if (pos >= sizeof(probe) - 1) { printf("name too long\n"); return 1; }
            probe[pos++] = ' ';
        }
        size_t n = strlen(argv[i]);
        if (pos + n >= sizeof(probe) - 1) { printf("name too long\n"); return 1; }
        for (size_t j = 0; j < n; j++) {
            probe[pos++] = (char)toupper((unsigned char)argv[i][j]);
        }
    }
    probe[pos] = '\0';

    denon_listening_mode_id_t id;
    if (denon_ms_parse(probe, &id) != DENON_TABLE_OK) {
        printf("unknown mode '%s'\n", probe + 2);
        return 1;
    }
    denon_intent_payload_t p = { .listening_mode_id = (uint8_t)id };
    submit_denon_intent(DENON_INTENT_SET_LISTENING_MODE, &p);
    return 0;
}

/* docs/ROADMAP.md Phase 3's exit criterion: "the safety clamp holds under
 * a fuzzed intent stream." A full uint16_t sweep (esp_random(), including
 * values with no legitimate origin) through the exact same
 * app_wiring_submit_intent() -> svc_denon_submit_intent() ->
 * denon_intent_to_cmd() path every real command uses. */
static int cmd_fuzz(int argc, char **argv)
{
    int n = (argc >= 3) ? atoi(argv[2]) : 100;
    if (n <= 0) {
        n = 100;
    }
    int dropped = 0;
    for (int i = 0; i < n; i++) {
        denon_intent_payload_t p = { .volume_half_db = (uint16_t)(esp_random() & 0xFFFFu) };
        intent_t intent = { 0 };
        intent.domain = INTENT_DOMAIN_DENON;
        intent.kind   = (uint16_t)DENON_INTENT_SET_VOLUME;
        memcpy(intent.payload, &p, sizeof(p));
        if (!app_wiring_submit_intent(g_ctx, &intent)) {
            dropped++;
        }
    }
    printf("fuzz: submitted %d volume intents (%d dropped, queue full)\n", n, dropped);
    return 0;
}

static int cmd_denon(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: denon status|host <ip>|power on/off|volume <half_db>|"
                "mute on/off|input <name>|mode <name...>|fuzz [n]\n");
        return 1;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "status") == 0) return cmd_status();
    if (strcmp(sub, "host") == 0)   return cmd_host(argc, argv);
    if (strcmp(sub, "power") == 0)  return cmd_power(argc, argv);
    if (strcmp(sub, "volume") == 0) return cmd_volume(argc, argv);
    if (strcmp(sub, "mute") == 0)   return cmd_mute(argc, argv);
    if (strcmp(sub, "input") == 0)  return cmd_input(argc, argv);
    if (strcmp(sub, "mode") == 0)   return cmd_mode(argc, argv);
    if (strcmp(sub, "fuzz") == 0)   return cmd_fuzz(argc, argv);

    printf("unknown subcommand '%s'\n", sub);
    return 1;
}

bool debug_console_start(app_ctx_t *ctx)
{
    g_ctx = ctx;

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt              = "dsc>";
    repl_config.max_cmdline_length = 128;
    /* Interactive dev tooling, not application work -- deliberately below
     * every real task's priority (ARCHITECTURE.md §4's table), so it never
     * contends with actual service logic. ESP_CONSOLE_REPL_CONFIG_DEFAULT()'s
     * own default (prio 2) collides with sched's; overridden explicitly. */
    repl_config.task_priority = 1;
    repl_config.task_core_id    = 0;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    if (esp_console_new_repl_uart(&uart_config, &repl_config, &repl) != ESP_OK) {
        ESP_LOGE(TAG, "esp_console_new_repl_uart failed");
        return false;
    }

    const esp_console_cmd_t denon_cmd = {
        .command = "denon",
        .help    = "status|host <ip>|power on/off|volume <half_db>|mute on/off|"
                    "input <name>|mode <name...>|fuzz [n]",
        .hint    = NULL,
        .func    = &cmd_denon,
    };
    if (esp_console_cmd_register(&denon_cmd) != ESP_OK) {
        ESP_LOGE(TAG, "esp_console_cmd_register failed");
        return false;
    }

#ifdef CONFIG_DSC_SPOTIFY_DEBUG_CONSOLE
    /* Registers onto this same REPL, not a second one -- see
     * debug_console_spotify.h's own top comment. */
    if (!debug_console_spotify_register(ctx)) {
        ESP_LOGE(TAG, "debug_console_spotify_register failed");
        return false;
    }
#endif

    if (esp_console_start_repl(repl) != ESP_OK) {
        ESP_LOGE(TAG, "esp_console_start_repl failed");
        return false;
    }

    ESP_LOGI(TAG, "debug console started");
    return true;
}
