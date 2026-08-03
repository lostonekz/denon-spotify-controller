/*
 * ctrl_queue/playback-intent draining, poll scheduling, the "spotify" task
 * entry point, teardown, restart, and the service descriptor. Target-only
 * -- see spotify_service.h's top comment.
 */
#include "svc_spotify_internal.h"

#include <string.h>

#include "esp_log.h"

#include "spotify_avr_match.h"
#include "spotify_intent_ack.h"
#include "spotify_poll_policy.h"

static const char *TAG = "svc_spotify";

/* strncpy() does not null-terminate when src is exactly dst_cap-1 bytes or
 * longer -- guaranteed to happen for Spotify's client_id, a fixed 32-hex-char
 * string that exactly fills SPOTIFY_CLIENT_ID_MAX-1. See spotify_service.c's
 * copy of this same helper for the real-hardware bug this fixes. */
static void safe_strncpy(char *dst, const char *src, size_t dst_cap)
{
    if (dst_cap == 0) {
        return;
    }
    strncpy(dst, src, dst_cap - 1);
    dst[dst_cap - 1] = '\0';
}

#define SPOTIFY_TASK_IDLE_DELAY_MS 100u   /* Tier -1's own busy-spin fix, applied from the
                                             * start here rather than discovered later --
                                             * this task has no blocking transport poll the
                                             * way Denon's does, so every idle iteration
                                             * needs an explicit sleep. */

static bool time_is_synced(svc_spotify_t *s)
{
    return s->time != NULL && s->time->synced;
}

static void handle_ctrl_cmd(svc_spotify_t *s, const spotify_ctrl_cmd_t *cmd)
{
    switch (cmd->kind) {
    case SPOTIFY_CTRL_CMD_PAIR:
        spotify_session_fsm_dispatch(&s->state.session_fsm, SPOTIFY_SESSION_EVT_PAIR_REQUESTED, NULL);
        break;

    case SPOTIFY_CTRL_CMD_CODE_PASTED:
        if (!time_is_synced(s)) {
            ESP_LOGW(TAG, "code paste ignored: clock not yet synced, retry shortly");
            break;
        }
        safe_strncpy(s->state.pending_code, cmd->arg, sizeof(s->state.pending_code));
        spotify_session_fsm_dispatch(&s->state.session_fsm, SPOTIFY_SESSION_EVT_CODE_PASTED, NULL);
        break;

    case SPOTIFY_CTRL_CMD_UNPAIR:
        spotify_session_fsm_dispatch(&s->state.session_fsm, SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED, NULL);
        break;

    case SPOTIFY_CTRL_CMD_SET_CLIENT_ID:
        xSemaphoreTake(s->session_lock, portMAX_DELAY);
        safe_strncpy(s->state.client_id, cmd->arg, sizeof(s->state.client_id));
        xSemaphoreGive(s->session_lock);
        if (!spotify_client_id_save(s->secure_kv, cmd->arg)) {
            ESP_LOGE(TAG, "failed to persist client_id");
        }
        break;

    case SPOTIFY_CTRL_CMD_SET_REDIRECT_URI:
        xSemaphoreTake(s->session_lock, portMAX_DELAY);
        safe_strncpy(s->state.redirect_uri, cmd->arg, sizeof(s->state.redirect_uri));
        xSemaphoreGive(s->session_lock);
        if (!spotify_redirect_uri_save(s->secure_kv, cmd->arg)) {
            ESP_LOGE(TAG, "failed to persist redirect_uri");
        }
        break;

    case SPOTIFY_CTRL_CMD_SET_AVR_DEVICE: {
        spotify_avr_pin_t pin = { 0 };
        safe_strncpy(pin.avr_device_id, cmd->arg, sizeof(pin.avr_device_id));

        xSemaphoreTake(s->session_lock, portMAX_DELAY);
        for (size_t i = 0; i < s->state.last_devices.count; i++) {
            if (strcmp(s->state.last_devices.devices[i].device_id, cmd->arg) == 0) {
                safe_strncpy(pin.avr_name, s->state.last_devices.devices[i].device_name, sizeof(pin.avr_name));
                break;
            }
        }
        s->state.avr_pin = pin;
        xSemaphoreGive(s->session_lock);

        if (!spotify_avr_pin_save(s->secure_kv, &pin)) {
            ESP_LOGE(TAG, "failed to persist AVR pin");
        }
        break;
    }

    case SPOTIFY_CTRL_CMD_CLEAR_AVR_DEVICE:
        xSemaphoreTake(s->session_lock, portMAX_DELAY);
        memset(&s->state.avr_pin, 0, sizeof(s->state.avr_pin));
        xSemaphoreGive(s->session_lock);
        spotify_avr_pin_clear(s->secure_kv);
        break;

    case SPOTIFY_CTRL_CMD_REFRESH_DEVICE_LIST: {
        if (spotify_session_fsm_current_state(&s->state.session_fsm) != SPOTIFY_SESSION_FSM_STATE_AUTHORIZED) {
            break;   /* no access token to call with */
        }
        spotify_device_list_t list = { 0 };
        spotify_api_result_t r = spotify_api_get_devices(s->http, &s->http_policy, s->state.access_token, &list);
        if (r == SPOTIFY_API_RESULT_OK) {
            xSemaphoreTake(s->session_lock, portMAX_DELAY);
            s->state.last_devices = list;
            xSemaphoreGive(s->session_lock);
        }
        break;
    }
    }
}

/* Phase 5 Tier -1: closes a real gap -- Phase 4's own plan called for
 * svc_spotify to publish TOPIC_INTENT_ACK, but a repo-wide search found the
 * type and topic policy wired and zero publishers anywhere. Without this,
 * EVENTS_AND_STATE.md §6.3's optimistic-UI protocol (a Now Playing tap
 * renders pending immediately) only ever resolves via its 3s abandon
 * timeout, never the fast, explicit path the mechanism exists for. */
static void publish_intent_ack(svc_spotify_t *s, uint32_t seq, intent_ack_evt_kind_t kind)
{
    event_t evt = { .topic = TOPIC_INTENT_ACK };
    evt.u.intent_ack.seq  = seq;
    evt.u.intent_ack.kind = kind;
    core_eventbus_publish(s->bus, &evt);
}

/* Force-poll at +250ms/+1s after any intent, per NETWORKING.md §1.3's
 * table -- converges the UI quickly rather than waiting for the next
 * routine poll. */
static void bump_force_poll(svc_spotify_t *s)
{
    int64_t now = s->clock.now_us(s->clock.ctx);
    s->state.force_poll_deadline_us[0] = spotify_poll_force_deadline_us(now, 0);
    s->state.force_poll_deadline_us[1] = spotify_poll_force_deadline_us(now, 1);
    s->state.force_poll_index = 0;
}

/* PLAYBACK_INTENT_RESUME_ON_AVR: "nothing is playing anywhere, wake the AVR
 * and resume" -- see playback_intent_types.h's own comment on this kind.
 * Bypasses spotify_intent_to_cmd() entirely: that translator is pure (no
 * svc_spotify_t access, host-tested against a fixed intent->cmd mapping)
 * and has no way to reach the persisted AVR device-id pin this needs, the
 * same reasoning documented on spotify_api_transfer_playback() itself. */
static void handle_resume_on_avr_intent(svc_spotify_t *s, const intent_t *intent)
{
    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    spotify_avr_pin_t pin = s->state.avr_pin;
    xSemaphoreGive(s->session_lock);

    if (pin.avr_device_id[0] == '\0') {
        /* Never paired an AVR device id (`spotify set-avr-device`) -- a
         * clean, real rejection, not a network failure, but surfaced the
         * same way (REJECTED ack -> toast) since there's nothing more
         * specific the existing ack machinery can say. */
        publish_intent_ack(s, intent->seq, INTENT_ACK_EVT_REJECTED);
        return;
    }

    spotify_api_result_t r = spotify_api_transfer_playback(s->http, &s->http_policy, s->state.access_token,
                                                                pin.avr_device_id, true);
    if (r == SPOTIFY_API_RESULT_UNAUTHORIZED) {
        s->state.access_token_refresh_due_us = s->clock.now_us(s->clock.ctx);
    }
    publish_intent_ack(s, intent->seq, spotify_intent_ack_kind_for_result(r));
    bump_force_poll(s);
}

static void handle_playback_intent(svc_spotify_t *s, const intent_t *intent)
{
    if (intent->domain == INTENT_DOMAIN_PLAYBACK &&
        (playback_intent_kind_t)intent->kind == PLAYBACK_INTENT_RESUME_ON_AVR) {
        handle_resume_on_avr_intent(s, intent);
        return;
    }

    spotify_api_cmd_t cmd;
    if (spotify_intent_to_cmd(intent, &cmd) != SPOTIFY_INTENT_RESULT_OK) {
        publish_intent_ack(s, intent->seq, INTENT_ACK_EVT_REJECTED);
        return;
    }

    spotify_api_result_t r = spotify_api_execute_cmd(s->http, &s->http_policy, s->state.access_token, &cmd);
    if (r == SPOTIFY_API_RESULT_UNAUTHORIZED) {
        s->state.access_token_refresh_due_us = s->clock.now_us(s->clock.ctx);   /* force a refresh now */
    }
    publish_intent_ack(s, intent->seq, spotify_intent_ack_kind_for_result(r));
    bump_force_poll(s);
}

static void publish_playback_event(svc_spotify_t *s, const playback_state_t *pb)
{
    event_t evt = { .topic = TOPIC_PLAYBACK };
    evt.u.playback.kind         = PLAYBACK_EVT_TRACK_CHANGED;
    evt.u.playback.is_playing  = pb->is_playing;
    evt.u.playback.shuffle     = pb->shuffle;
    evt.u.playback.repeat_mode = pb->repeat_mode;
    evt.u.playback.position_ms = pb->position_ms;
    core_eventbus_publish(s->bus, &evt);
}

static void handle_poll_result(svc_spotify_t *s, spotify_api_result_t r, const playback_state_t *pb)
{
    if (r == SPOTIFY_API_RESULT_UNAUTHORIZED) {
        s->state.access_token_refresh_due_us = s->clock.now_us(s->clock.ctx);
        s->state.consecutive_poll_errors     = 0;
        return;
    }
    if (r == SPOTIFY_API_RESULT_FAILED) {
        s->state.consecutive_poll_errors++;
        s->state.next_poll_due_us = s->clock.now_us(s->clock.ctx) +
                                       spotify_poll_backoff_us(s->state.consecutive_poll_errors);
        return;
    }
    s->state.consecutive_poll_errors = 0;

    if (r == SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE) {
        playback_state_t empty = { .device_is_avr = DEVICE_IS_AVR_UNKNOWN };
        core_state_write(s->state_store, DOMAIN_PLAYBACK, LINK_STATUS_OK, &empty, sizeof(empty));
        /* Phase 5 Tier -1: this transition was previously silent -- only
         * the "something is playing" branch below ever published. Stop
         * playback entirely and nothing downstream (svc_artwork, Phase
         * 5's UI) ever learned playback had stopped, so stale
         * metadata/artwork stuck on screen forever. */
        publish_playback_event(s, &empty);
    } else {
        playback_state_t resolved = *pb;

        xSemaphoreTake(s->session_lock, portMAX_DELAY);
        spotify_avr_pin_t pin          = s->state.avr_pin;
        spotify_device_list_t devices = s->state.last_devices;
        xSemaphoreGive(s->session_lock);

        spotify_device_ref_t refs[SPOTIFY_PARSE_MAX_DEVICES];
        for (size_t i = 0; i < devices.count; i++) {
            refs[i].device_id   = devices.devices[i].device_id;
            refs[i].device_name = devices.devices[i].device_name;
        }
        spotify_avr_match_outcome_t match = spotify_avr_match_resolve(
            resolved.device_id, resolved.device_name,
            pin.avr_device_id, pin.avr_name, refs, devices.count);

        resolved.device_is_avr = (match.result == SPOTIFY_AVR_MATCH_YES) ? DEVICE_IS_AVR_YES
                                    : (match.result == SPOTIFY_AVR_MATCH_NO) ? DEVICE_IS_AVR_NO
                                                                                : DEVICE_IS_AVR_UNKNOWN;
        if (match.self_heal_device_id != NULL) {
            xSemaphoreTake(s->session_lock, portMAX_DELAY);
            strncpy(s->state.avr_pin.avr_device_id, match.self_heal_device_id,
                     sizeof(s->state.avr_pin.avr_device_id) - 1);
            spotify_avr_pin_t heal_pin = s->state.avr_pin;
            xSemaphoreGive(s->session_lock);
            spotify_avr_pin_save(s->secure_kv, &heal_pin);
            ESP_LOGI(TAG, "AVR pin self-healed to a new device id (name matched)");
        }

        core_state_write(s->state_store, DOMAIN_PLAYBACK, LINK_STATUS_OK, &resolved, sizeof(resolved));
        publish_playback_event(s, &resolved);
    }

    /* svc_power doesn't exist until Phase 6 -- hardcoded awake, per Phase 4
     * plan §1 (spotify_poll_policy.c's "asleep" conditions are implemented
     * and tested but unreachable from here until then). */
    spotify_poll_condition_t cond = {
        .is_playing   = (r != SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE) && pb->is_playing,
        .screen_awake = true,
    };
    s->state.next_poll_due_us = s->clock.now_us(s->clock.ctx) + spotify_poll_interval_us(&cond);
}

void spotify_service_tick(svc_spotify_t *s)
{
    spotify_ctrl_cmd_t cmd;
    while (xQueueReceive(s->ctrl_queue, &cmd, 0) == pdTRUE) {
        handle_ctrl_cmd(s, &cmd);
    }

    if (spotify_session_fsm_current_state(&s->state.session_fsm) != SPOTIFY_SESSION_FSM_STATE_AUTHORIZED) {
        /* Phase 5 Tier -1: no access token to act with -- drain and reject
         * rather than leaving a posted intent stuck un-acked until
         * whenever re-auth happens (which may be indefinite). The queue
         * used to be left entirely untouched here. */
        intent_t stuck;
        while (xQueueReceive(s->playback_intent_queue, &stuck, 0) == pdTRUE) {
            publish_intent_ack(s, stuck.seq, INTENT_ACK_EVT_REJECTED);
        }
        return;   /* nothing else below needs a valid access token */
    }

    intent_t intent;
    if (xQueueReceive(s->playback_intent_queue, &intent, 0) == pdTRUE) {
        handle_playback_intent(s, &intent);
    }

    int64_t now = s->clock.now_us(s->clock.ctx);
    bool due = (now >= s->state.next_poll_due_us);
    if (!due && s->state.force_poll_index >= 0 && s->state.force_poll_index < SPOTIFY_POLL_FORCE_POLL_COUNT &&
        now >= s->state.force_poll_deadline_us[s->state.force_poll_index]) {
        due = true;
    }
    if (due) {
        playback_state_t pb = { 0 };
        spotify_api_result_t r = spotify_api_get_player_state(s->http, &s->http_policy, s->state.access_token, &pb);
        handle_poll_result(s, r, &pb);

        if (s->state.force_poll_index >= 0) {
            s->state.force_poll_index++;
            if (s->state.force_poll_index >= SPOTIFY_POLL_FORCE_POLL_COUNT) {
                s->state.force_poll_index = -1;
            }
        }
    }
}

void spotify_service_task(void *pv)
{
    svc_spotify_t *s = (svc_spotify_t *)pv;
    while (!s->task_should_stop) {
        spotify_service_session_drive(s);
        spotify_service_tick(s);
        vTaskDelay(pdMS_TO_TICKS(SPOTIFY_TASK_IDLE_DELAY_MS));
    }
    xSemaphoreGive(s->stopped_sem);
    vTaskDelete(NULL);
}

void svc_spotify_teardown(void *handle)
{
    svc_spotify_t *s = (svc_spotify_t *)handle;

    if (s->task_handle != NULL) {
        s->task_should_stop = true;
        xSemaphoreTake(s->stopped_sem, portMAX_DELAY);
        s->task_handle = NULL;
    }
    if (s->ctrl_queue != NULL) {
        vQueueDelete(s->ctrl_queue);
        s->ctrl_queue = NULL;
    }
    if (s->playback_intent_queue != NULL) {
        vQueueDelete(s->playback_intent_queue);
        s->playback_intent_queue = NULL;
    }
    if (s->session_lock != NULL) {
        vSemaphoreDelete(s->session_lock);
        s->session_lock = NULL;
    }
    if (s->stopped_sem != NULL) {
        vSemaphoreDelete(s->stopped_sem);
        s->stopped_sem = NULL;
    }

    spotify_service_state_reset(&s->state);
}

bool svc_spotify_restart(void *restart_ctx, void **out_handle)
{
    svc_spotify_restart_ctx_t *rc = (svc_spotify_restart_ctx_t *)restart_ctx;

    svc_spotify_teardown(rc->s);
    if (!svc_spotify_create(rc->s, &rc->cfg)) {
        return false;
    }
    *out_handle = rc->s;
    return true;
}

service_desc_t svc_spotify_service_desc(svc_spotify_restart_ctx_t *restart_ctx)
{
    /* LIFECYCLE.md §1.2's own worked example -- svc_spotify verbatim:
     * RESTART_BACKOFF_THEN_DEGRADE/max_restarts=3/degraded_ok=true. */
    service_desc_t desc = {
        .id           = SVC_ID_SPOTIFY,
        .name         = "svc_spotify",
        .requires     = SVC_BIT(SVC_ID_STORAGE) | SVC_BIT(SVC_ID_NET_WIFI) | SVC_BIT(SVC_ID_NET_TIME),
        .restart      = RESTART_BACKOFF_THEN_DEGRADE,
        .max_restarts = 3,
        .degraded_ok  = true,
        .teardown     = svc_spotify_teardown,
        .restart_fn   = svc_spotify_restart,
        .restart_ctx  = restart_ctx,
    };
    return desc;
}
