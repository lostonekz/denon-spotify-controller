/*
 * See app_wiring_core.h. Split out of app_wiring.c for the 400-line gate.
 */
#include "app_wiring_core.h"

#include "esp_log.h"

static const char *TAG = "app_wiring";

bool app_wiring_create_bus(app_ctx_t *ctx, os_lock_if_t bus_lock)
{
    ctx->bus_topic_policies[0] = (topic_policy_t){
        .topic = TOPIC_SETTINGS, .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    ctx->bus_topic_policies[1] = (topic_policy_t){
        .topic = TOPIC_DIAG, .depth = 4, .on_full = EVENT_OVERFLOW_DROP_NEWEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    ctx->bus_topic_policies[2] = (topic_policy_t){
        .topic = TOPIC_NET, .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    ctx->bus_topic_policies[3] = (topic_policy_t){
        .topic = TOPIC_TIME, .depth = 2, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    ctx->bus_topic_policies[4] = (topic_policy_t){
        .topic = TOPIC_OTA, .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    ctx->bus_topic_policies[5] = (topic_policy_t){
        .topic = TOPIC_DENON, .depth = 8, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    ctx->bus_topic_policies[6] = (topic_policy_t){
        .topic = TOPIC_PLAYBACK, .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    ctx->bus_topic_policies[7] = (topic_policy_t){
        .topic = TOPIC_SPOTIFY, .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    /* on_release = NULL, deliberately -- NOT artwork_cache_release as an
     * earlier draft of EVENTS_AND_STATE.md's own table implies. svc_artwork
     * alone releases handles, on its own schedule (see artwork_cache.h's
     * ownership contract); wiring up a bus-drop-triggered release too would
     * double-release the first time the bus ever dropped an undelivered
     * ARTWORK_EVT_READY event. Tier 7 corrects the doc. */
    ctx->bus_topic_policies[8] = (topic_policy_t){
        .topic = TOPIC_ARTWORK, .depth = 2, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    ctx->bus_topic_policies[9] = (topic_policy_t){
        .topic = TOPIC_INTENT_ACK, .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    /* Phase 5 Tier 6: EVENTS_AND_STATE.md §5.3's own table already specced
     * this (depth 4, drop-newest) but nothing had registered it -- w_toast
     * (components/ui) is the first real subscriber
     * (view_model_now_playing_apply_init()). Without this entry that
     * subscribe() call fails (core_eventbus_subscribe()'s own contract:
     * "topic has no entry in topic_policies"), which fails ui_app_create(),
     * which fails app_wiring_create_ui() -- i.e. this is boot-fatal, not a
     * cosmetic gap. */
    ctx->bus_topic_policies[10] = (topic_policy_t){
        .topic = TOPIC_TOAST, .depth = 4, .on_full = EVENT_OVERFLOW_DROP_NEWEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    /* Phase 6: declared in EVENTS_AND_STATE.md §5.4 since Phase 1, first
     * given a producer (svc_power) and a policy here. Replace-oldest
     * because only the newest power state matters -- a dropped intermediate
     * ACTIVE/ASLEEP is not a lost user action, unlike TOPIC_TOAST above.
     * Depth 4 is generous for a topic that changes at human timescales; it
     * costs 4 event_t of caller-owned storage per subscriber and buys
     * immunity to a relay tick being late. */
    ctx->bus_topic_policies[11] = (topic_policy_t){
        .topic = TOPIC_POWER, .depth = 4, .on_full = EVENT_OVERFLOW_REPLACE_OLDEST,
        .on_release = NULL, .release_ctx = NULL,
    };
    core_eventbus_init(&ctx->bus, ctx->bus_subs, APP_EVENTBUS_MAX_SUBS,
                        ctx->bus_topic_policies, APP_EVENTBUS_TOPIC_POLICIES,
                        bus_lock);

    /* Subscribed before anything (svc_settings, created below) can
     * possibly publish to TOPIC_SETTINGS -- denon_settings_relay_tick_trampoline
     * drains this. */
    if (!core_eventbus_subscribe(&ctx->bus, TOPIC_SETTINGS, ctx->denon_settings_sub_storage,
                                   sizeof(ctx->denon_settings_sub_storage) / sizeof(ctx->denon_settings_sub_storage[0]),
                                   &ctx->denon_settings_sub)) {
        ESP_LOGE(TAG, "denon settings subscription failed");
        return false;
    }
    /* Subscribed before svc_ota (created below) can possibly publish --
     * ota_artwork_relay_tick_trampoline drains this. */
    if (!core_eventbus_subscribe(&ctx->bus, TOPIC_OTA, ctx->ota_artwork_relay_sub_storage,
                                   sizeof(ctx->ota_artwork_relay_sub_storage) / sizeof(ctx->ota_artwork_relay_sub_storage[0]),
                                   &ctx->ota_artwork_relay_sub)) {
        ESP_LOGE(TAG, "ota/artwork relay subscription failed");
        return false;
    }
    return true;
}

bool app_wiring_create_logging(app_ctx_t *ctx)
{
    /* Both the no-space and space-after-colon JSON forms are listed since
     * svc_logging_redact_apply() is pure prefix matching with no JSON
     * awareness -- either form could appear depending on serializer
     * formatting. */
    static const redact_rule_t LOG_REDACT_RULES[] = {
        { .prefix = "Authorization: Bearer " },
        { .prefix = "\"access_token\":\"" },
        { .prefix = "\"access_token\": \"" },
        { .prefix = "\"refresh_token\":\"" },
        { .prefix = "\"refresh_token\": \"" },
        { .prefix = "access_token=" },
        { .prefix = "refresh_token=" },
        { .prefix = "code_verifier=" },
        { .prefix = "code=" },
    };
    if (!svc_logging_create(&ctx->logging, LOG_REDACT_RULES,
                              sizeof(LOG_REDACT_RULES) / sizeof(LOG_REDACT_RULES[0]))) {
        ESP_LOGE(TAG, "svc_logging_create failed");
        return false;
    }
    return true;
}
