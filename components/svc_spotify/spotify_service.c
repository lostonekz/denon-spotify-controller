/*
 * svc_spotify_create() and the setters/getters. Target-only -- see
 * spotify_service.h's top comment. The actual host-tested "create" logic
 * is spotify_service_state_init(), called from here as one step among
 * several (transport wiring, real actions install, task+mutex creation) --
 * same shape as svc_denon_create().
 */
#include "svc_spotify_internal.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"

#include "spotify_auth.h"
#include "spotify_paste_guard.h"

static const char *TAG = "svc_spotify";

/* strncpy() does not null-terminate when src is exactly dst_cap-1 bytes or
 * longer -- guaranteed to happen for Spotify's client_id, a fixed 32-hex-char
 * string that exactly fills SPOTIFY_CLIENT_ID_MAX-1. Found on real hardware:
 * an unterminated local client_id copy read straight into the adjacent stack
 * variable during a later %s substitution, silently corrupting the built
 * authorize URL. Used for every fixed-size string copy in this file. */
static void safe_strncpy(char *dst, const char *src, size_t dst_cap)
{
    if (dst_cap == 0) {
        return;
    }
    strncpy(dst, src, dst_cap - 1);
    dst[dst_cap - 1] = '\0';
}

static const char *session_state_name(uint32_t s)
{
    switch ((spotify_session_fsm_state_t)s) {
    case SPOTIFY_SESSION_FSM_STATE_UNPAIRED:        return "UNPAIRED";
    case SPOTIFY_SESSION_FSM_STATE_AWAITING_CODE:   return "AWAITING_CODE";
    case SPOTIFY_SESSION_FSM_STATE_EXCHANGING_CODE: return "EXCHANGING_CODE";
    case SPOTIFY_SESSION_FSM_STATE_RESUMING:        return "RESUMING";
    case SPOTIFY_SESSION_FSM_STATE_AUTHORIZED:      return "AUTHORIZED";
    case SPOTIFY_SESSION_FSM_STATE_REFRESHING:      return "REFRESHING";
    case SPOTIFY_SESSION_FSM_STATE_REAUTH_REQUIRED: return "REAUTH_REQUIRED";
    default:                                        return "?";
    }
}

static const char *session_event_name(uint32_t e)
{
    switch ((spotify_session_fsm_event_t)e) {
    case SPOTIFY_SESSION_EVT_PAIR_REQUESTED:          return "PAIR_REQUESTED";
    case SPOTIFY_SESSION_EVT_CODE_PASTED:             return "CODE_PASTED";
    case SPOTIFY_SESSION_EVT_EXCHANGE_OK:             return "EXCHANGE_OK";
    case SPOTIFY_SESSION_EVT_EXCHANGE_FAILED:         return "EXCHANGE_FAILED";
    case SPOTIFY_SESSION_EVT_RESUME_DUE:              return "RESUME_DUE";
    case SPOTIFY_SESSION_EVT_RESUME_OK:               return "RESUME_OK";
    case SPOTIFY_SESSION_EVT_RESUME_FAILED_AUTH:      return "RESUME_FAILED_AUTH";
    case SPOTIFY_SESSION_EVT_RESUME_FAILED_TRANSIENT: return "RESUME_FAILED_TRANSIENT";
    case SPOTIFY_SESSION_EVT_REFRESH_DUE:             return "REFRESH_DUE";
    case SPOTIFY_SESSION_EVT_REFRESH_OK:              return "REFRESH_OK";
    case SPOTIFY_SESSION_EVT_REFRESH_FAILED_AUTH:     return "REFRESH_FAILED_AUTH";
    case SPOTIFY_SESSION_EVT_REFRESH_FAILED_TRANSIENT: return "REFRESH_FAILED_TRANSIENT";
    case SPOTIFY_SESSION_EVT_UNPAIR_REQUESTED:        return "UNPAIR_REQUESTED";
    default:                                          return "?";
    }
}

void spotify_session_fsm_transition_logger(void *log_ctx, const char *fsm_name,
                                              uint32_t from_state, uint32_t event, uint32_t to_state)
{
    (void)log_ctx;
    ESP_LOGI(TAG, "[fsm:%s] %s --(%s)--> %s", fsm_name,
              session_state_name(from_state), session_event_name(event), session_state_name(to_state));
}

bool svc_spotify_create(svc_spotify_t *s, const svc_spotify_cfg_t *cfg)
{
    memset(s, 0, sizeof(*s));

    s->http        = cfg->http;
    s->time        = cfg->time;
    s->bus         = cfg->bus;
    s->state_store = cfg->state_store;
    s->clock       = cfg->clock;
    s->http_policy = cfg->http_policy;
    s->secure_kv   = cfg->secure_kv;

    s->session_lock = xSemaphoreCreateMutex();
    if (s->session_lock == NULL) {
        return false;
    }

    if (!spotify_service_state_init(&s->state, &cfg->state_cfg)) {
        vSemaphoreDelete(s->session_lock);
        return false;
    }

    /* Direct field assignment, not a re-call of spotify_service_state_init()
     * -- see that function's own comment for why re-init would be wrong
     * here (it would reset current_state back to initial_state). */
    spotify_session_fsm_actions_t real = spotify_service_session_real_actions();
    real.ctx = s;
    s->state.session_fsm.actions = real;

    s->ctrl_queue = xQueueCreate(SPOTIFY_CTRL_QUEUE_DEPTH, sizeof(spotify_ctrl_cmd_t));
    s->playback_intent_queue = xQueueCreate(SPOTIFY_PLAYBACK_INTENT_QUEUE_DEPTH, sizeof(intent_t));
    if (s->ctrl_queue == NULL || s->playback_intent_queue == NULL) {
        vSemaphoreDelete(s->session_lock);
        return false;
    }

    s->stopped_sem = xSemaphoreCreateBinary();
    if (s->stopped_sem == NULL) {
        vSemaphoreDelete(s->session_lock);
        return false;
    }

    /* One response buffer for this service's lifetime, instead of a
     * heap_caps_malloc()/free() pair inside every single API call (see
     * SPOTIFY_RESPONSE_BUF_BYTES in spotify_api.h). PSRAM-backed for the
     * same reason this task's own stack is: internal SRAM is the scarce
     * resource here and 16 KiB of it, held permanently, is not affordable
     * (MEMORY.md §1/§4). */
    s->resp_buf = heap_caps_malloc(SPOTIFY_RESPONSE_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (s->resp_buf == NULL) {
        ESP_LOGE(TAG, "response buffer alloc failed");
        vSemaphoreDelete(s->session_lock);
        vSemaphoreDelete(s->stopped_sem);
        s->stopped_sem = NULL;
        return false;
    }

    s->task_should_stop = false;
    /* PSRAM-backed stack -- MEMORY.md §4's real-hardware finding: real
     * internal-SRAM margin is only ~138 KiB (idf.py size's DIRAM total),
     * and this is the task whose own /v1/me/player poll shares the exact
     * same real mbedTLS-buffer-vs-internal-RAM failure caught on artwork's
     * fetch from the real Spotify CDN. Same xTaskCreatePinnedToCoreWithCaps()
     * pattern already proven safe for ui_app's stack. */
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(spotify_service_task, "spotify", SPOTIFY_TASK_STACK, s,
                                                       SPOTIFY_TASK_PRIO, &s->task_handle, SPOTIFY_TASK_CORE,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        vSemaphoreDelete(s->session_lock);
        vSemaphoreDelete(s->stopped_sem);
        s->stopped_sem = NULL;
        heap_caps_free(s->resp_buf);
        s->resp_buf = NULL;
        return false;
    }
    return true;
}

bool svc_spotify_submit_intent(svc_spotify_t *s, const intent_t *intent)
{
    if (intent->domain != INTENT_DOMAIN_PLAYBACK) {
        return false;
    }
    return xQueueSend(s->playback_intent_queue, intent, 0) == pdTRUE;
}

bool svc_spotify_ctrl_submit(svc_spotify_t *s, spotify_ctrl_cmd_kind_t kind, const char *arg)
{
    spotify_ctrl_cmd_t cmd = { .kind = kind };
    if (arg != NULL) {
        safe_strncpy(cmd.arg, arg, sizeof(cmd.arg));
    }
    return xQueueSend(s->ctrl_queue, &cmd, 0) == pdTRUE;
}

void svc_spotify_get_status(svc_spotify_t *s, spotify_session_summary_t *out_status,
                               bool *out_reauth_recommended, char out_avr_id[SPOTIFY_AVR_DEVICE_ID_MAX],
                               char out_avr_name[SPOTIFY_AVR_NAME_MAX])
{
    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    if (out_status != NULL) *out_status = s->state.session_status;
    if (out_reauth_recommended != NULL) *out_reauth_recommended = s->state.reauth_recommended;
    if (out_avr_id != NULL) safe_strncpy(out_avr_id, s->state.avr_pin.avr_device_id, SPOTIFY_AVR_DEVICE_ID_MAX);
    if (out_avr_name != NULL) safe_strncpy(out_avr_name, s->state.avr_pin.avr_name, SPOTIFY_AVR_NAME_MAX);
    xSemaphoreGive(s->session_lock);
}

size_t svc_spotify_get_last_devices(svc_spotify_t *s, spotify_device_entry_t *out, size_t out_cap)
{
    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    size_t n = s->state.last_devices.count;
    if (n > out_cap) {
        n = out_cap;
    }
    memcpy(out, s->state.last_devices.devices, n * sizeof(spotify_device_entry_t));
    xSemaphoreGive(s->session_lock);
    return n;
}

bool svc_spotify_begin_pair(svc_spotify_t *s, char out_authorize_url[SPOTIFY_AUTHORIZE_URL_MAX])
{
    return svc_spotify_begin_pair_with(s, NULL, NULL, out_authorize_url);
}

bool svc_spotify_begin_pair_with(svc_spotify_t *s, const char *fixed_verifier, const char *fixed_state,
                                    char out_authorize_url[SPOTIFY_AUTHORIZE_URL_MAX])
{
    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    char client_id[SPOTIFY_CLIENT_ID_MAX];
    char redirect_uri[SPOTIFY_REDIRECT_URI_MAX];
    safe_strncpy(client_id, s->state.client_id, sizeof(client_id));
    safe_strncpy(redirect_uri, s->state.redirect_uri, sizeof(redirect_uri));
    xSemaphoreGive(s->session_lock);

    if (client_id[0] == '\0' || redirect_uri[0] == '\0') {
        return false;
    }

    spotify_auth_pending_pair_t pending;
    char                           state_nonce[SPOTIFY_STATE_NONCE_MAX];
    if (!spotify_auth_begin_pair_ex(client_id, redirect_uri, fixed_verifier, fixed_state,
                                       out_authorize_url, &pending, state_nonce)) {
        return false;
    }

    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    s->state.pending_pair = pending;
    safe_strncpy(s->state.pending_state, state_nonce, sizeof(s->state.pending_state));
    xSemaphoreGive(s->session_lock);

    return svc_spotify_ctrl_submit(s, SPOTIFY_CTRL_CMD_PAIR, NULL);
}

bool svc_spotify_submit_pasted_url(svc_spotify_t *s, const char *url)
{
    char code[SPOTIFY_CTRL_CMD_ARG_MAX];
    char state_param[SPOTIFY_STATE_NONCE_MAX];
    /* Raw (not percent-decoded) extraction -- this project's PKCE
     * code/state values are hex/base64url, which need no escaping in a
     * query string, so a full URL-decoder would be unused complexity for
     * what a manually-pasted redirect URL actually contains. */
    if (!spotify_paste_extract_param(url, "code", code, sizeof(code)) ||
        !spotify_paste_extract_param(url, "state", state_param, sizeof(state_param))) {
        return false;
    }

    xSemaphoreTake(s->session_lock, portMAX_DELAY);
    bool match = spotify_paste_state_matches(s->state.pending_state, state_param);
    xSemaphoreGive(s->session_lock);

    if (!match) {
        return false;
    }

    return svc_spotify_ctrl_submit(s, SPOTIFY_CTRL_CMD_CODE_PASTED, code);
}

bool svc_spotify_suspend(svc_spotify_t *s)
{
    /* API-completeness only (ARCHITECTURE.md §1.3's DI shape) -- not wired
     * to the OTA-coexistence relay, see Phase 4 plan §8. A brief transient
     * stall on the TLS arbiter during an OTA download is accepted the same
     * way Denon's HTTP fallback already accepts it. */
    (void)s;
    return true;
}

bool svc_spotify_resume(svc_spotify_t *s)
{
    (void)s;
    return true;
}
