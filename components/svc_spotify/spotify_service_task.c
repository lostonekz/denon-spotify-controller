/*
 * ctrl_queue/playback-intent draining, poll scheduling, and the "spotify"
 * task entry point. Target-only -- see spotify_service.h's top comment.
 *
 * Teardown, restart, the session-close request and the service descriptor
 * live in spotify_service_lifecycle.c -- split out purely for the 400-line
 * gate, along the seam this file's own comment already described.
 */
#include "svc_spotify_internal.h"

#include <string.h>

#include "esp_log.h"

#include "spotify_avr_match.h"
#include "spotify_intent_ack.h"
#include "spotify_poll_policy.h"

static const char *TAG = "svc_spotify";


/* Upper bound on how long playback commands are answered locally after
 * Spotify reports no active device. Deliberately generous relative to
 * the 5s poll: the poll clears this the moment a device reappears
 * (handle_poll_result), so in practice the suppression ends on the next
 * poll, and this only bounds the case where polling itself is struggling. */
#define SPOTIFY_NO_DEVICE_SUPPRESS_US ((int64_t)10 * 1000 * 1000)

#define SPOTIFY_TASK_IDLE_DELAY_MS 100u   /* Tier -1's own busy-spin fix, applied from the
                                             * start here rather than discovered later --
                                             * this task has no blocking transport poll the
                                             * way Denon's does, so every idle iteration
                                             * needs an explicit sleep. */



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
        ESP_LOGW(TAG, "intent seq=%u rejected: no AVR device pinned "
                       "(run `spotify set-avr-device`) -- nothing to resume playback on",
                  (unsigned)intent->seq);
        publish_intent_ack(s, intent->seq, INTENT_ACK_EVT_REJECTED);
        return;
    }

    spotify_api_result_t r = spotify_api_transfer_playback(s->http, &s->http_policy, s->state.access_token,
                                                                pin.avr_device_id, true);
    if (r == SPOTIFY_API_RESULT_UNAUTHORIZED) {
        s->state.access_token_refresh_due_us = s->clock.now_us(s->clock.ctx);
    }
    if (r != SPOTIFY_API_RESULT_OK) {
        ESP_LOGW(TAG, "intent seq=%u: transfer_playback result=%d", (unsigned)intent->seq, (int)r);
    }
    publish_intent_ack(s, intent->seq, spotify_intent_ack_kind_for_result(r));
    bump_force_poll(s);
}

static void handle_playback_intent(svc_spotify_t *s, const intent_t *intent)
{
    /* Answer locally while we know there is nothing to command. Every
     * send here costs a full TLS handshake (a fresh connection per
     * request), and the rejection is exactly what prompts the user to
     * press again -- measured on real hardware as six handshakes in ~11s
     * driving free internal RAM down to 891 bytes. The ack is identical
     * to the one the network round trip would have produced, so the UI
     * behaves the same; it just arrives instantly and for free.
     *
     * RESUME_ON_AVR is deliberately NOT suppressed: it is the one command
     * whose entire purpose is to CREATE an active device, so blocking it
     * would make this state unrecoverable from the panel. */
    if (!(intent->domain == INTENT_DOMAIN_PLAYBACK &&
          (playback_intent_kind_t)intent->kind == PLAYBACK_INTENT_RESUME_ON_AVR) &&
        s->no_device_suppress_until_us != 0 &&
        s->clock.now_us(s->clock.ctx) < s->no_device_suppress_until_us) {
        ESP_LOGD(TAG, "intent seq=%u answered locally: no active device (suppressed)",
                  (unsigned)intent->seq);
        publish_intent_ack(s, intent->seq, INTENT_ACK_EVT_NO_ACTIVE_DEVICE);
        return;
    }

    if (intent->domain == INTENT_DOMAIN_PLAYBACK &&
        (playback_intent_kind_t)intent->kind == PLAYBACK_INTENT_RESUME_ON_AVR) {
        handle_resume_on_avr_intent(s, intent);
        return;
    }

    spotify_api_cmd_t cmd;
    if (spotify_intent_to_cmd(intent, &cmd) != SPOTIFY_INTENT_RESULT_OK) {
        /* Same silent-toast gap as the two other rejection paths in this
         * file: an intent kind this translator has no mapping for is a
         * build-time wiring mistake, not a network problem, and should not
         * be indistinguishable from one in the log. */
        ESP_LOGW(TAG, "intent seq=%u rejected: no cmd mapping (domain=%d kind=%d)",
                  (unsigned)intent->seq, (int)intent->domain, (int)intent->kind);
        publish_intent_ack(s, intent->seq, INTENT_ACK_EVT_REJECTED);
        return;
    }

    spotify_api_result_t r = spotify_api_execute_cmd(s->http, &s->http_policy, s->state.access_token, &cmd);
    if (r == SPOTIFY_API_RESULT_UNAUTHORIZED) {
        s->state.access_token_refresh_due_us = s->clock.now_us(s->clock.ctx);   /* force a refresh now */
    }
    if (r == SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE) {
        /* Arm the local-answer window; the poll clears it as soon as a
         * device is visible again. */
        s->no_device_suppress_until_us =
            s->clock.now_us(s->clock.ctx) + SPOTIFY_NO_DEVICE_SUPPRESS_US;
        ESP_LOGW(TAG, "intent seq=%u: no active device -- suppressing further "
                       "commands for up to %ds to avoid a TLS handshake storm",
                  (unsigned)intent->seq, (int)(SPOTIFY_NO_DEVICE_SUPPRESS_US / 1000000));
    } else if (r == SPOTIFY_API_RESULT_OK) {
        s->no_device_suppress_until_us = 0;
    }
    if (r != SPOTIFY_API_RESULT_OK) {
        /* Dump what the last successful poll believed about the target
         * device alongside the failure. A 404 from a player-command
         * endpoint is Spotify's "no active device", but that is a claim
         * about Spotify's own view, which can disagree with what is
         * audibly playing in the room -- e.g. a device playing via the
         * receiver's own HEOS/Spotify integration rather than as a
         * Connect target this token can command, or one flagged
         * is_restricted (DeviceObject.is_restricted, app_state_types.h:
         * "the Web API cannot control this device at all"). Without these
         * fields the log cannot distinguish those cases from each other. */
        playback_state_t dbg = { 0 };
        core_state_read(s->state_store, DOMAIN_PLAYBACK, &dbg, sizeof(dbg));
        ESP_LOGW(TAG, "intent seq=%u: execute_cmd result=%d | device id='%s' name='%s' "
                       "is_avr=%d restricted=%d supports_vol=%d playing=%d",
                  (unsigned)intent->seq, (int)r,
                  dbg.device_id, dbg.device_name, (int)dbg.device_is_avr,
                  (int)dbg.device_is_restricted, (int)dbg.device_supports_volume,
                  (int)dbg.is_playing);
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

    /* Log only when the answer CHANGES -- a poll runs every 5s, so
     * logging each one would drown the console, but the edges are the
     * whole story: the exact poll at which Spotify stops seeing an
     * active device is what turns "it randomly stops working" into a
     * timestamp that can be lined up against whatever preceded it. */
    bool has_device = (r != SPOTIFY_API_RESULT_NO_ACTIVE_DEVICE);
    if (!s->poll_ever_reported || has_device != s->poll_had_device) {
        s->poll_ever_reported = true;
        s->poll_had_device    = has_device;
        if (has_device) {
            /* A device is back -- lift any command suppression immediately
             * rather than making the user wait out the window. */
            s->no_device_suppress_until_us = 0;
            ESP_LOGW(TAG, "poll: device APPEARED name='%s' id='%s' playing=%d restricted=%d",
                      pb->device_name, pb->device_id, (int)pb->is_playing,
                      (int)pb->device_is_restricted);
        } else {
            ESP_LOGW(TAG, "poll: device DISAPPEARED (Spotify reports no active device)");
        }
    }

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
        spotify_service_handle_ctrl_cmd(s, &cmd);
    }

    if (spotify_session_fsm_current_state(&s->state.session_fsm) != SPOTIFY_SESSION_FSM_STATE_AUTHORIZED) {
        /* Phase 5 Tier -1: no access token to act with -- drain and reject
         * rather than leaving a posted intent stuck un-acked until
         * whenever re-auth happens (which may be indefinite). The queue
         * used to be left entirely untouched here. */
        intent_t stuck;
        while (xQueueReceive(s->playback_intent_queue, &stuck, 0) == pdTRUE) {
            /* Logged, not silent: this rejection reaches the user as the
             * "Couldn't reach Spotify" toast, and until now left no trace at
             * all -- the session simply not being AUTHORIZED is a completely
             * different problem from a failed network call, but both looked
             * identical from the outside. */
            ESP_LOGW(TAG, "intent seq=%u rejected: session not AUTHORIZED (fsm state %d)",
                      (unsigned)stuck.seq,
                      (int)spotify_session_fsm_current_state(&s->state.session_fsm));
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
        spotify_api_result_t r = spotify_api_get_player_state(s->http, &s->http_policy, s->state.access_token,
                                                                   s->resp_buf, SPOTIFY_RESPONSE_BUF_BYTES, &pb);
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
