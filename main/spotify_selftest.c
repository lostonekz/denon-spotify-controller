/*
 * See spotify_selftest.h. Target-only, one-shot task, only compiled in
 * behind CONFIG_DSC_SPOTIFY_AUTOPAIR.
 */
#include "spotify_selftest.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

static const char *TAG = "spotify_selftest";

#define SELFTEST_TASK_STACK        4096
#define SELFTEST_TASK_PRIO         1
#define SELFTEST_TIME_SYNC_TIMEOUT_MS 30000
#define SELFTEST_PAIRING_TIMEOUT_MS   10000
#define SELFTEST_STATUS_POLL_MS       2000
#define SELFTEST_STATUS_TIMEOUT_MS    30000

static const char *status_name(spotify_session_summary_t s)
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

static spotify_session_summary_t current_status(app_ctx_t *ctx)
{
    spotify_session_summary_t status;
    svc_spotify_get_status(&ctx->svc_spotify, &status, NULL, NULL, NULL);
    return status;
}

static void wait_for_time_sync(app_ctx_t *ctx)
{
    ESP_LOGI(TAG, "waiting for SNTP sync (up to %d ms)...", SELFTEST_TIME_SYNC_TIMEOUT_MS);
    int waited = 0;
    while (waited < SELFTEST_TIME_SYNC_TIMEOUT_MS) {
        if (ctx->net_time.synced) {
            ESP_LOGI(TAG, "time synced after %d ms", waited);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        waited += 500;
    }
    ESP_LOGW(TAG, "time never synced within timeout -- pairing will be deferred by svc_spotify itself");
}

/* Polls status until it reaches `want` or the timeout elapses. Returns true
 * on a match. */
static bool wait_for_status(app_ctx_t *ctx, spotify_session_summary_t want, int timeout_ms)
{
    int waited = 0;
    while (waited < timeout_ms) {
        if (current_status(ctx) == want) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        waited += 200;
    }
    return false;
}

static void selftest_task(void *pv)
{
    app_ctx_t *ctx = (app_ctx_t *)pv;

    wait_for_time_sync(ctx);

    if (current_status(ctx) == SPOTIFY_SESSION_ACTIVE) {
        ESP_LOGI(TAG, "already paired (session=ACTIVE) -- nothing to do");
        goto done;
    }

    /* Uses DSC_SPOTIFY_AUTOPAIR_VERIFIER/_STATE unconditionally, not a
     * freshly random verifier/state -- a two-boot pairing attempt (print
     * URL, reflash with the code) needs the code_challenge/state Spotify
     * saw on the FIRST boot to still match on the SECOND, and fixing them
     * as source-level constants guarantees that regardless of anything
     * (NVS, a live console write) surviving the reflash in between. */
    ESP_LOGI(TAG, "step 1: begin pairing (equivalent to `spotify pair`) using DSC_SPOTIFY_AUTOPAIR's "
                    "fixed verifier/state");
    char authorize_url[SPOTIFY_AUTHORIZE_URL_MAX];
    if (!svc_spotify_begin_pair_with(&ctx->svc_spotify, CONFIG_DSC_SPOTIFY_AUTOPAIR_VERIFIER,
                                        CONFIG_DSC_SPOTIFY_AUTOPAIR_STATE, authorize_url)) {
        ESP_LOGE(TAG, "begin_pair failed -- check DSC_SPOTIFY_CLIENT_ID/DSC_SPOTIFY_REDIRECT_URI "
                        "are set in Kconfig");
        goto done;
    }

    if (CONFIG_DSC_SPOTIFY_AUTOPAIR_CODE[0] == '\0') {
        ESP_LOGI(TAG, "open this URL, log in, approve access, then paste the FULL address-bar URL "
                        "you land on (even though the page itself fails to load):");
        /* Printed in short chunks, deliberately -- a single ~280-char ESP_LOGI
         * call was observed to corrupt reproducibly over this board's native
         * USB-Serial/JTAG console (same byte offset, same garbled character,
         * every time, regardless of the underlying random values -- ruling
         * out transmission noise). Each chunk here stays well under whatever
         * length triggers it; concatenate the printed [n] pieces in order to
         * reconstruct the URL. */
        size_t url_len = strlen(authorize_url);
        const size_t chunk = 32;
        for (size_t off = 0; off < url_len; off += chunk) {
            size_t n = url_len - off;
            if (n > chunk) {
                n = chunk;
            }
            char piece[chunk + 1];
            memcpy(piece, authorize_url + off, n);
            piece[n] = '\0';
            ESP_LOGI(TAG, "[%u] %s", (unsigned)(off / chunk), piece);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        ESP_LOGI(TAG, "DSC_SPOTIFY_AUTOPAIR_CODE is empty -- stopping here. Paste the redirect URL "
                        "you get back into DSC_SPOTIFY_AUTOPAIR_CODE and reflash to finish pairing.");
        goto done;
    }

    ESP_LOGI(TAG, "step 2: DSC_SPOTIFY_AUTOPAIR_CODE is set -- waiting for AWAITING_CODE, then submitting it");
    if (!wait_for_status(ctx, SPOTIFY_SESSION_PAIRING, SELFTEST_PAIRING_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "never reached PAIRING state within timeout -- submitting anyway");
    }
    if (!svc_spotify_submit_pasted_url(&ctx->svc_spotify, CONFIG_DSC_SPOTIFY_AUTOPAIR_CODE)) {
        ESP_LOGE(TAG, "submit_pasted_url failed -- no code/state in DSC_SPOTIFY_AUTOPAIR_CODE, or its "
                        "&state=... doesn't match DSC_SPOTIFY_AUTOPAIR_STATE (did the URL come from a "
                        "pair attempt made with a different DSC_SPOTIFY_AUTOPAIR_STATE value?)");
        goto done;
    }
    ESP_LOGI(TAG, "code submitted, exchanging...");

    ESP_LOGI(TAG, "step 3: waiting for session status (up to %d ms)...", SELFTEST_STATUS_TIMEOUT_MS);
    {
        int waited = 0;
        spotify_session_summary_t last = SPOTIFY_SESSION_UNPAIRED;
        while (waited < SELFTEST_STATUS_TIMEOUT_MS) {
            spotify_session_summary_t now = current_status(ctx);
            if (now != last) {
                ESP_LOGI(TAG, "session status -> %s", status_name(now));
                last = now;
            }
            if (now == SPOTIFY_SESSION_ACTIVE || now == SPOTIFY_SESSION_REAUTH_REQUIRED) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(SELFTEST_STATUS_POLL_MS));
            waited += SELFTEST_STATUS_POLL_MS;
        }
    }

done:
    ESP_LOGI(TAG, "SPOTIFY SELFTEST COMPLETE (final status=%s)", status_name(current_status(ctx)));
    vTaskDelete(NULL);
}

void spotify_selftest_start(app_ctx_t *ctx)
{
    TaskHandle_t handle = NULL;
    BaseType_t ok = xTaskCreatePinnedToCore(selftest_task, "spotify_selftest", SELFTEST_TASK_STACK,
                                              ctx, SELFTEST_TASK_PRIO, &handle, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create selftest task");
    }
}
