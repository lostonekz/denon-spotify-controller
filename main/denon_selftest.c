/*
 * See denon_selftest.h. Target-only, one-shot task, only compiled in
 * behind CONFIG_DSC_DENON_SELFTEST.
 */
#include "denon_selftest.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_random.h"

#include "app_wiring.h"
#include "denon_codec_tables.h"
#include "denon_intent.h"

static const char *TAG = "denon_selftest";

#define SELFTEST_TASK_STACK          4096
#define SELFTEST_TASK_PRIO           1
#define SELFTEST_LINK_WAIT_TIMEOUT_MS 60000
#define SELFTEST_STEP_DELAY_MS       1500
#define SELFTEST_FUZZ_COUNT          50

static void submit(app_ctx_t *ctx, denon_intent_kind_t kind,
                     const denon_intent_payload_t *payload, const char *desc)
{
    intent_t intent = { 0 };
    intent.domain = INTENT_DOMAIN_DENON;
    intent.kind   = (uint16_t)kind;
    memcpy(intent.payload, payload, sizeof(*payload));
    bool ok = app_wiring_submit_intent(ctx, &intent);
    ESP_LOGI(TAG, "submit %s -> %s", desc, ok ? "queued" : "DROPPED (queue full)");
}

static void wait_for_link(app_ctx_t *ctx)
{
    ESP_LOGI(TAG, "waiting for denon link (up to %d ms)...", SELFTEST_LINK_WAIT_TIMEOUT_MS);
    int waited = 0;
    while (waited < SELFTEST_LINK_WAIT_TIMEOUT_MS) {
        denon_link_state_t s = svc_denon_link_state(&ctx->svc_denon);
        if (s == DENON_LINK_STATE_CONNECTED_TELNET || s == DENON_LINK_STATE_CONNECTED_HTTP) {
            ESP_LOGI(TAG, "link up (state=%d) after %d ms -- starting scripted sequence",
                      (int)s, waited);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        waited += 500;
    }
    ESP_LOGW(TAG, "link never came up within timeout -- running the sequence anyway "
                   "(commands will just queue)");
}

static void selftest_task(void *pv)
{
    app_ctx_t *ctx = (app_ctx_t *)pv;
    denon_intent_payload_t p;

    wait_for_link(ctx);

    ESP_LOGI(TAG, "step 1: power on");
    memset(&p, 0, sizeof(p));
    p.power_on = true;
    submit(ctx, DENON_INTENT_SET_POWER, &p, "SET_POWER(on)");
    vTaskDelay(pdMS_TO_TICKS(SELFTEST_STEP_DELAY_MS));

    ESP_LOGI(TAG, "step 2: input CD");
    memset(&p, 0, sizeof(p));
    p.input_id = DENON_INPUT_CD;
    submit(ctx, DENON_INTENT_SET_INPUT, &p, "SET_INPUT(CD)");
    vTaskDelay(pdMS_TO_TICKS(SELFTEST_STEP_DELAY_MS));

    ESP_LOGI(TAG, "step 3: volume 60 (30.0dB, safely under any reasonable limit)");
    memset(&p, 0, sizeof(p));
    p.volume_half_db = 60;
    submit(ctx, DENON_INTENT_SET_VOLUME, &p, "SET_VOLUME(60)");
    vTaskDelay(pdMS_TO_TICKS(SELFTEST_STEP_DELAY_MS));

    ESP_LOGI(TAG, "step 4: SAFETY CLAMP CHECK -- requesting 197 half-dB (98.5dB, the codec's own max)");
    ESP_LOGI(TAG, "step 4: watch the 'sent: MVxxx' line below -- it must NOT be MV985");
    memset(&p, 0, sizeof(p));
    p.volume_half_db = 197;
    submit(ctx, DENON_INTENT_SET_VOLUME, &p, "SET_VOLUME(197, should clamp)");
    vTaskDelay(pdMS_TO_TICKS(SELFTEST_STEP_DELAY_MS));

    ESP_LOGI(TAG, "step 5: mute on");
    memset(&p, 0, sizeof(p));
    p.muted = true;
    submit(ctx, DENON_INTENT_SET_MUTE, &p, "SET_MUTE(on)");
    vTaskDelay(pdMS_TO_TICKS(SELFTEST_STEP_DELAY_MS));

    ESP_LOGI(TAG, "step 6: mute off");
    memset(&p, 0, sizeof(p));
    p.muted = false;
    submit(ctx, DENON_INTENT_SET_MUTE, &p, "SET_MUTE(off)");
    vTaskDelay(pdMS_TO_TICKS(SELFTEST_STEP_DELAY_MS));

    ESP_LOGI(TAG, "step 7: listening mode MOVIE");
    memset(&p, 0, sizeof(p));
    p.listening_mode_id = DENON_MODE_MOVIE;
    submit(ctx, DENON_INTENT_SET_LISTENING_MODE, &p, "SET_LISTENING_MODE(MOVIE)");
    vTaskDelay(pdMS_TO_TICKS(SELFTEST_STEP_DELAY_MS));

    ESP_LOGI(TAG, "step 8: fuzzed volume burst (%d intents, full uint16_t range) -- "
                   "docs/ROADMAP.md Phase 3's exit criterion, on real hardware", SELFTEST_FUZZ_COUNT);
    int dropped = 0;
    for (int i = 0; i < SELFTEST_FUZZ_COUNT; i++) {
        memset(&p, 0, sizeof(p));
        p.volume_half_db = (uint16_t)(esp_random() & 0xFFFFu);
        intent_t intent = { 0 };
        intent.domain = INTENT_DOMAIN_DENON;
        intent.kind   = (uint16_t)DENON_INTENT_SET_VOLUME;
        memcpy(intent.payload, &p, sizeof(p));
        if (!app_wiring_submit_intent(ctx, &intent)) {
            dropped++;
        }
    }
    ESP_LOGI(TAG, "fuzz burst done: %d submitted, %d dropped (queue full)",
              SELFTEST_FUZZ_COUNT, dropped);

    vTaskDelay(pdMS_TO_TICKS(3000));   /* let cmdq drain the fuzz burst before declaring done */
    ESP_LOGI(TAG, "SELFTEST COMPLETE");
    vTaskDelete(NULL);
}

void denon_selftest_start(app_ctx_t *ctx)
{
    TaskHandle_t handle = NULL;
    BaseType_t ok = xTaskCreatePinnedToCore(selftest_task, "denon_selftest", SELFTEST_TASK_STACK,
                                              ctx, SELFTEST_TASK_PRIO, &handle, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create selftest task");
    }
}
