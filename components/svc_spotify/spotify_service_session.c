/*
 * spotify_session_fsm's real actions (target-only: mbedtls/esp_random via
 * spotify_auth.c, real HTTP calls via spotify_api.c/spotify_auth.c) plus
 * the drive loop that turns each action's synchronous result into a
 * follow-up dispatch. See spotify_service.h's top comment for the
 * cross-boundary discipline this mirrors from denon_service_link.c.
 */
#include "svc_spotify_internal.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"

#include "spotify_auth.h"
#include "spotify_reauth_watch.h"

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

/* Not specified anywhere -- a judgement call, matching denon_link_fsm's
 * own precedent for undocumented backoff intervals. */
#define SPOTIFY_RESUME_BACKOFF_US ((int64_t)10 * 1000000)

static bool time_is_synced(svc_spotify_t *s)
{
    return s->time != NULL && s->time->synced;
}

static void publish_and_write_state(svc_spotify_t *s)
{
    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    spotify_state_t st = { 0 };
    st.authenticated      = (s->state.session_status == SPOTIFY_SESSION_ACTIVE);
    st.session_status     = s->state.session_status;
    st.reauth_recommended = s->state.reauth_recommended;
    xSemaphoreGive(s->session_lock);

    core_state_write(s->state_store, DOMAIN_SPOTIFY, LINK_STATUS_OK, &st, sizeof(st));

    event_t evt = { .topic = TOPIC_SPOTIFY };
    evt.u.spotify.kind = SPOTIFY_EVT_SESSION_STATE;
    core_eventbus_publish(s->bus, &evt);
}

static void set_session_status(svc_spotify_t *s, spotify_session_summary_t status)
{
    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    s->state.session_status = status;
    xSemaphoreGive(s->session_lock);
    publish_and_write_state(s);
}

static void schedule_proactive_refresh(svc_spotify_t *s, uint32_t expires_in_s)
{
    /* R4/NETWORKING.md §1.3: proactive refresh at 80% of expires_in, not
     * on a 401. */
    int64_t now = s->clock.now_us(s->clock.ctx);
    s->state.access_token_refresh_due_us = now + (int64_t)expires_in_s * 800000;   /* 0.8 * 1e6 */
}

/* --- actions ------------------------------------------------------------ */

static void action_start_pair(void *ctx)
{
    /* No network/PKCE work here -- the console already generated the
     * verifier/challenge/state and printed the authorize URL synchronously
     * (fast, local) before enqueueing this -- see spotify_service.h's own
     * comment on SPOTIFY_CTRL_CMD_PAIR. Still needs to update the projected
     * session_status, though: found on real hardware that this action being
     * otherwise empty left session_status stuck at whatever it was before
     * (SPOTIFY_SESSION_UNPAIRED) despite the raw FSM state correctly
     * reaching AWAITING_CODE -- `spotify status`/svc_spotify_get_status()
     * callers would see a stale UNPAIRED throughout the entire pairing
     * window. */
    set_session_status((svc_spotify_t *)ctx, SPOTIFY_SESSION_PAIRING);
}

static void action_start_exchange(void *ctx)
{
    svc_spotify_t *s = (svc_spotify_t *)ctx;

    if (!time_is_synced(s)) {
        ESP_LOGW(TAG, "code exchange deferred: clock not yet synced");
        s->state.token_op_result   = SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT;
        s->state.token_op_just_ran = true;
        return;
    }

    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    char client_id[SPOTIFY_CLIENT_ID_MAX];
    char redirect_uri[SPOTIFY_REDIRECT_URI_MAX];
    char code_verifier[SPOTIFY_PKCE_VERIFIER_MAX];
    safe_strncpy(client_id, s->state.client_id, sizeof(client_id));
    safe_strncpy(redirect_uri, s->state.redirect_uri, sizeof(redirect_uri));
    safe_strncpy(code_verifier, s->state.pending_pair.code_verifier, sizeof(code_verifier));
    xSemaphoreGive(s->session_lock);

    /* s->state.pending_code is task-only (set by the tick handler just
     * before dispatching CODE_PASTED), no lock needed. */
    s->state.token_op_result = spotify_auth_exchange_code(s->http, &s->http_policy, client_id, redirect_uri,
                                                              s->state.pending_code, code_verifier,
                                                              &s->state.token_op_tokens);
    s->state.token_op_just_ran = true;
}

static void action_enter_authorized_first_time(void *ctx)
{
    svc_spotify_t *s = (svc_spotify_t *)ctx;

    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    safe_strncpy(s->state.refresh_token, s->state.token_op_tokens.refresh_token, sizeof(s->state.refresh_token));
    s->state.paired_at_epoch_s = time(NULL);
    xSemaphoreGive(s->session_lock);

    safe_strncpy(s->state.access_token, s->state.token_op_tokens.access_token, sizeof(s->state.access_token));
    schedule_proactive_refresh(s, s->state.token_op_tokens.expires_in_s);

    spotify_session_credentials_t creds = { 0 };
    safe_strncpy(creds.refresh_token, s->state.token_op_tokens.refresh_token, sizeof(creds.refresh_token));
    creds.paired_at_epoch_s = s->state.paired_at_epoch_s;
    if (!spotify_credentials_save(s->secure_kv, &creds)) {
        ESP_LOGE(TAG, "failed to persist refresh token after pairing");
    }

    ESP_LOGI(TAG, "paired successfully");
    set_session_status(s, SPOTIFY_SESSION_ACTIVE);
}

static void action_report_exchange_failed(void *ctx)
{
    svc_spotify_t *s = (svc_spotify_t *)ctx;
    ESP_LOGW(TAG, "code exchange failed -- run `spotify pair` again for a fresh code");
    set_session_status(s, SPOTIFY_SESSION_UNPAIRED);
}

static void do_resume_or_refresh(svc_spotify_t *s)
{
    if (!time_is_synced(s)) {
        ESP_LOGW(TAG, "token refresh deferred: clock not yet synced");
        s->state.token_op_result   = SPOTIFY_AUTH_RESULT_FAILED_TRANSIENT;
        s->state.token_op_just_ran = true;
        return;
    }

    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    char client_id[SPOTIFY_CLIENT_ID_MAX];
    char refresh_token[SPOTIFY_REFRESH_TOKEN_MAX];
    safe_strncpy(client_id, s->state.client_id, sizeof(client_id));
    safe_strncpy(refresh_token, s->state.refresh_token, sizeof(refresh_token));
    xSemaphoreGive(s->session_lock);

    s->state.token_op_result = spotify_auth_refresh(s->http, &s->http_policy, client_id, refresh_token,
                                                        &s->state.token_op_tokens);
    s->state.token_op_just_ran = true;
}

static void action_start_resume(void *ctx)  { do_resume_or_refresh((svc_spotify_t *)ctx); }
static void action_start_refresh(void *ctx) { do_resume_or_refresh((svc_spotify_t *)ctx); }

static void action_enter_authorized_resumed(void *ctx)
{
    svc_spotify_t *s = (svc_spotify_t *)ctx;

    safe_strncpy(s->state.access_token, s->state.token_op_tokens.access_token, sizeof(s->state.access_token));
    schedule_proactive_refresh(s, s->state.token_op_tokens.expires_in_s);

    /* Spotify may or may not rotate the refresh token -- only persist (and
     * only update the in-memory copy) if it actually did. paired_at_epoch_s
     * is deliberately untouched -- it tracks the original pairing, not the
     * most recent refresh (Phase 4 plan §2). */
    if (s->state.token_op_tokens.refresh_token[0] != '\0') {
        xSemaphoreTake(s->session_lock, portMAX_DELAY);
        safe_strncpy(s->state.refresh_token, s->state.token_op_tokens.refresh_token, sizeof(s->state.refresh_token));
        int64_t paired_at = s->state.paired_at_epoch_s;
        xSemaphoreGive(s->session_lock);

        spotify_session_credentials_t creds = { 0 };
        safe_strncpy(creds.refresh_token, s->state.token_op_tokens.refresh_token, sizeof(creds.refresh_token));
        creds.paired_at_epoch_s = paired_at;
        if (!spotify_credentials_save(s->secure_kv, &creds)) {
            ESP_LOGE(TAG, "failed to persist rotated refresh token");
        }
    }

    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    int64_t paired_at = s->state.paired_at_epoch_s;
    xSemaphoreGive(s->session_lock);

    spotify_reauth_status_t reauth = spotify_reauth_check(paired_at, time(NULL));
    set_session_status(s, SPOTIFY_SESSION_ACTIVE);
    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    s->state.reauth_recommended = (reauth == SPOTIFY_REAUTH_DUE_SOON);
    xSemaphoreGive(s->session_lock);
}

static void action_enter_reauth_required(void *ctx)
{
    svc_spotify_t *s = (svc_spotify_t *)ctx;
    ESP_LOGE(TAG, "refresh token rejected (invalid_grant) -- re-pairing required");
    s->state.access_token[0] = '\0';
    set_session_status(s, SPOTIFY_SESSION_REAUTH_REQUIRED);
}

static void action_report_resume_failed_transient(void *ctx)
{
    svc_spotify_t *s = (svc_spotify_t *)ctx;
    ESP_LOGW(TAG, "resume failed transiently -- retrying with backoff");
    s->state.resume_backoff_deadline_us = s->clock.now_us(s->clock.ctx) + SPOTIFY_RESUME_BACKOFF_US;
}

static void action_report_refresh_failed_transient(void *ctx)
{
    svc_spotify_t *s = (svc_spotify_t *)ctx;
    ESP_LOGW(TAG, "proactive refresh failed transiently -- keeping current access token, retrying sooner");
    /* Retry sooner than the normal 80%-of-expiry schedule, but don't spin:
     * a fixed short delay, same judgement-call reasoning as the resume
     * backoff above. */
    s->state.access_token_refresh_due_us = s->clock.now_us(s->clock.ctx) + SPOTIFY_RESUME_BACKOFF_US;
}

static void action_unpair(void *ctx)
{
    svc_spotify_t *s = (svc_spotify_t *)ctx;

    /* AVR pin deliberately NOT cleared here -- it identifies a piece of
     * hardware, not a session (Phase 4 plan §9); only set-avr-device/
     * clear-avr-device touch it. */
    if (!spotify_credentials_clear(s->secure_kv)) {
        ESP_LOGE(TAG, "failed to clear stored credentials");
    }

    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    memset(s->state.refresh_token, 0, sizeof(s->state.refresh_token));
    s->state.paired_at_epoch_s = 0;
    xSemaphoreGive(s->session_lock);
    s->state.access_token[0] = '\0';

    ESP_LOGI(TAG, "unpaired");
    set_session_status(s, SPOTIFY_SESSION_UNPAIRED);
}

spotify_session_fsm_actions_t spotify_service_session_real_actions(void)
{
    spotify_session_fsm_actions_t actions = {
        .start_pair                      = action_start_pair,
        .start_exchange                  = action_start_exchange,
        .enter_authorized_first_time     = action_enter_authorized_first_time,
        .report_exchange_failed          = action_report_exchange_failed,
        .start_resume                    = action_start_resume,
        .enter_authorized_resumed        = action_enter_authorized_resumed,
        .enter_reauth_required           = action_enter_reauth_required,
        .report_resume_failed_transient  = action_report_resume_failed_transient,
        .start_refresh                   = action_start_refresh,
        .report_refresh_failed_transient = action_report_refresh_failed_transient,
        .unpair                          = action_unpair,
        .ctx                              = NULL,   /* filled in by svc_spotify_create() */
    };
    return actions;
}

void spotify_service_session_drive(svc_spotify_t *s)
{
    bool changed = true;
    while (changed) {
        changed = false;

        if (s->state.token_op_just_ran) {
            s->state.token_op_just_ran = false;
            spotify_session_fsm_state_t st = spotify_session_fsm_current_state(&s->state.session_fsm);
            spotify_session_fsm_event_t evt;

            if (st == SPOTIFY_SESSION_FSM_STATE_EXCHANGING_CODE) {
                evt = (s->state.token_op_result == SPOTIFY_AUTH_RESULT_OK) ? SPOTIFY_SESSION_EVT_EXCHANGE_OK
                                                                              : SPOTIFY_SESSION_EVT_EXCHANGE_FAILED;
            } else if (st == SPOTIFY_SESSION_FSM_STATE_RESUMING) {
                if (s->state.token_op_result == SPOTIFY_AUTH_RESULT_OK) {
                    evt = SPOTIFY_SESSION_EVT_RESUME_OK;
                } else if (s->state.token_op_result == SPOTIFY_AUTH_RESULT_FAILED_AUTH) {
                    evt = SPOTIFY_SESSION_EVT_RESUME_FAILED_AUTH;
                } else {
                    evt = SPOTIFY_SESSION_EVT_RESUME_FAILED_TRANSIENT;
                }
            } else if (st == SPOTIFY_SESSION_FSM_STATE_REFRESHING) {
                if (s->state.token_op_result == SPOTIFY_AUTH_RESULT_OK) {
                    evt = SPOTIFY_SESSION_EVT_REFRESH_OK;
                } else if (s->state.token_op_result == SPOTIFY_AUTH_RESULT_FAILED_AUTH) {
                    evt = SPOTIFY_SESSION_EVT_REFRESH_FAILED_AUTH;
                } else {
                    evt = SPOTIFY_SESSION_EVT_REFRESH_FAILED_TRANSIENT;
                }
            } else {
                continue;   /* stale flag from a state we've since left via UNPAIR_REQUESTED -- drop it */
            }
            spotify_session_fsm_dispatch(&s->state.session_fsm, evt, NULL);
            changed = true;
            continue;
        }

        spotify_session_fsm_state_t st = spotify_session_fsm_current_state(&s->state.session_fsm);
        int64_t now = s->clock.now_us(s->clock.ctx);

        if (s->state.resume_pending && st == SPOTIFY_SESSION_FSM_STATE_RESUMING) {
            s->state.resume_pending = false;
            spotify_session_fsm_dispatch(&s->state.session_fsm, SPOTIFY_SESSION_EVT_RESUME_DUE, NULL);
            changed = true;
            continue;
        }
        if (st == SPOTIFY_SESSION_FSM_STATE_RESUMING && !s->state.resume_pending &&
            now >= s->state.resume_backoff_deadline_us) {
            spotify_session_fsm_dispatch(&s->state.session_fsm, SPOTIFY_SESSION_EVT_RESUME_DUE, NULL);
            changed = true;
            continue;
        }
        if (st == SPOTIFY_SESSION_FSM_STATE_AUTHORIZED && now >= s->state.access_token_refresh_due_us) {
            spotify_session_fsm_dispatch(&s->state.session_fsm, SPOTIFY_SESSION_EVT_REFRESH_DUE, NULL);
            changed = true;
            continue;
        }
    }
}
