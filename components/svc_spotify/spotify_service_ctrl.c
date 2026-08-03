/*
 * The console/ctrl-command surface of svc_spotify: pairing, credential
 * and AVR-pin updates, and the on-demand device-list refresh. Target-only.
 *
 * Split out of spotify_service_task.c for this project's 400-line limit.
 * The seam is a real one rather than an arbitrary cut: everything here is
 * driven by an operator typing a command, on a path with no realtime
 * constraint, whereas what remains in spotify_service_task.c is the
 * per-iteration polling and playback-intent hot path.
 */
#include "svc_spotify_internal.h"

#include <string.h>

#include "esp_log.h"

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

static bool time_is_synced(svc_spotify_t *s)
{
    return s->time != NULL && s->time->synced;
}

void spotify_service_handle_ctrl_cmd(svc_spotify_t *s, const spotify_ctrl_cmd_t *cmd)
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
        spotify_api_result_t r = spotify_api_get_devices(s->http, &s->http_policy, s->state.access_token,
                                                              s->resp_buf, SPOTIFY_RESPONSE_BUF_BYTES, &list);
        if (r == SPOTIFY_API_RESULT_OK) {
            xSemaphoreTake(s->session_lock, portMAX_DELAY);
            s->state.last_devices = list;
            xSemaphoreGive(s->session_lock);
        }
        break;
    }
    }
}
