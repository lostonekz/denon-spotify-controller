#include "net_wifi_internal.h"

#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/idf_additions.h"

#define NET_WIFI_TASK_STACK 4096
#define NET_WIFI_TASK_PRIO  6

static const char *TAG = "net_wifi";

/* --- FSM transition logging (EVENTS_AND_STATE.md §6.4: "every transition
 * is logged... this log is the entire debugging story") -- shared by both
 * net_wifi_fsm_t and net_wifi_prov_fsm_t, since both pass their own
 * fsm_cfg_t.name ("net_wifi"/"provisioning") to fsm_transition_log_fn. --- */

static const char *net_wifi_state_name(uint32_t s)
{
    switch ((net_wifi_state_t)s) {
    case NET_WIFI_STATE_BOOT:          return "BOOT";
    case NET_WIFI_STATE_PROVISIONING:  return "PROVISIONING";
    case NET_WIFI_STATE_CONNECTING:    return "CONNECTING";
    case NET_WIFI_STATE_SYNCING:       return "SYNCING";
    case NET_WIFI_STATE_ONLINE:        return "ONLINE";
    case NET_WIFI_STATE_DEGRADED:      return "DEGRADED";
    case NET_WIFI_STATE_RECONNECTING:  return "RECONNECTING";
    default:                          return "?";
    }
}

static const char *net_wifi_event_name(uint32_t e)
{
    switch ((net_wifi_event_t)e) {
    case NET_WIFI_EVT_NO_CREDENTIALS:      return "NO_CREDENTIALS";
    case NET_WIFI_EVT_HAVE_CREDENTIALS:    return "HAVE_CREDENTIALS";
    case NET_WIFI_EVT_CREDENTIALS_SAVED:   return "CREDENTIALS_SAVED";
    case NET_WIFI_EVT_GOT_IP:              return "GOT_IP";
    case NET_WIFI_EVT_CONNECT_FAILED:      return "CONNECT_FAILED";
    case NET_WIFI_EVT_SNTP_OK:             return "SNTP_OK";
    case NET_WIFI_EVT_SNTP_TIMEOUT:        return "SNTP_TIMEOUT";
    case NET_WIFI_EVT_LINK_LOST:           return "LINK_LOST";
    case NET_WIFI_EVT_DNS_FAIL:            return "DNS_FAIL";
    case NET_WIFI_EVT_API_ERRORS_EXCEEDED: return "API_ERRORS_EXCEEDED";
    case NET_WIFI_EVT_BEGIN_RECONNECT:     return "BEGIN_RECONNECT";
    case NET_WIFI_EVT_RETRY_NOW:           return "RETRY_NOW";
    default:                              return "?";
    }
}

static const char *net_wifi_prov_state_name(uint32_t s)
{
    switch ((net_wifi_prov_state_t)s) {
    case PROV_STATE_IDLE:         return "IDLE";
    case PROV_STATE_AP_STARTING:  return "AP_STARTING";
    case PROV_STATE_AP_ACTIVE:    return "AP_ACTIVE";
    case PROV_STATE_SAVING:       return "SAVING";
    case PROV_STATE_AP_STOPPING:  return "AP_STOPPING";
    case PROV_STATE_DONE:         return "DONE";
    case PROV_STATE_FAILED:       return "FAILED";
    default:                     return "?";
    }
}

static const char *net_wifi_prov_event_name(uint32_t e)
{
    switch ((net_wifi_prov_event_t)e) {
    case PROV_EVT_START:                 return "START";
    case PROV_EVT_AP_UP:                 return "AP_UP";
    case PROV_EVT_AP_START_FAILED:       return "AP_START_FAILED";
    case PROV_EVT_CREDENTIALS_SUBMITTED: return "CREDENTIALS_SUBMITTED";
    case PROV_EVT_SAVE_OK:               return "SAVE_OK";
    case PROV_EVT_SAVE_FAILED:           return "SAVE_FAILED";
    case PROV_EVT_AP_STOPPED:            return "AP_STOPPED";
    default:                            return "?";
    }
}

static void net_wifi_fsm_transition_logger(void *log_ctx, const char *fsm_name,
                                             uint32_t from_state, uint32_t event, uint32_t to_state)
{
    (void)log_ctx;
    if (strcmp(fsm_name, "net_wifi") == 0) {
        ESP_LOGI(TAG, "[fsm:%s] %s --(%s)--> %s", fsm_name,
                  net_wifi_state_name(from_state), net_wifi_event_name(event), net_wifi_state_name(to_state));
    } else {
        ESP_LOGI(TAG, "[fsm:%s] %s --(%s)--> %s", fsm_name,
                  net_wifi_prov_state_name(from_state), net_wifi_prov_event_name(event),
                  net_wifi_prov_state_name(to_state));
    }
}

/* --- messaging (safe from any task context) --- */

void net_wifi_post_msg(net_wifi_t *w, net_wifi_msg_type_t type, bool ok)
{
    net_wifi_msg_t msg = { .type = type, .ok = ok };
    if (xQueueSend(w->msg_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "message queue full, dropped msg type=%d", (int)type);
    }
}

static void retry_timer_cb(TimerHandle_t timer)
{
    net_wifi_t *w = (net_wifi_t *)pvTimerGetTimerID(timer);
    net_wifi_post_msg(w, NET_WIFI_MSG_RETRY_TIMER, true);
}

static uint32_t rand_u32_wrapper(void *ctx)
{
    (void)ctx;
    return esp_random();
}

/* --- message handling (net_sup task, the only caller of *_fsm_dispatch) --- */

static void handle_credentials_result(net_wifi_t *w, bool ok)
{
    net_wifi_prov_fsm_dispatch(&w->prov_fsm, PROV_EVT_CREDENTIALS_SUBMITTED, NULL);
    if (ok) {
        net_wifi_prov_fsm_dispatch(&w->prov_fsm, PROV_EVT_SAVE_OK, NULL);      /* stop_ap runs here */
        net_wifi_prov_fsm_dispatch(&w->prov_fsm, PROV_EVT_AP_STOPPED, NULL);   /* done runs here */
    } else {
        net_wifi_prov_fsm_dispatch(&w->prov_fsm, PROV_EVT_SAVE_FAILED, NULL);
    }
}

static void handle_retry_timer(net_wifi_t *w)
{
    switch (net_wifi_fsm_current_state(&w->fsm)) {
    case NET_WIFI_STATE_RECONNECTING:
        net_wifi_fsm_dispatch(&w->fsm, NET_WIFI_EVT_RETRY_NOW, NULL);
        break;
    case NET_WIFI_STATE_SYNCING:
        net_wifi_fsm_dispatch(&w->fsm, NET_WIFI_EVT_SNTP_TIMEOUT, NULL);
        break;
    default:
        break;   /* stale timer fire (state moved on before it elapsed) */
    }
}

static void handle_disconnected(net_wifi_t *w)
{
    net_wifi_state_t state = net_wifi_fsm_current_state(&w->fsm);
    if (state == NET_WIFI_STATE_CONNECTING) {
        net_wifi_fsm_dispatch(&w->fsm, NET_WIFI_EVT_CONNECT_FAILED, NULL);
    } else if (state == NET_WIFI_STATE_SYNCING || state == NET_WIFI_STATE_ONLINE) {
        net_wifi_fsm_dispatch(&w->fsm, NET_WIFI_EVT_LINK_LOST, NULL);
    }
    /* Anything else (BOOT/PROVISIONING/DEGRADED/RECONNECTING) has no STA
     * connection to lose in the first place. */
}

static void handle_msg(net_wifi_t *w, const net_wifi_msg_t *msg)
{
    switch (msg->type) {
    case NET_WIFI_MSG_STA_CONNECTED:
        break;   /* association only -- GOT_IP is what actually matters */
    case NET_WIFI_MSG_STA_DISCONNECTED:
        handle_disconnected(w);
        break;
    case NET_WIFI_MSG_GOT_IP:
        net_wifi_fsm_dispatch(&w->fsm, NET_WIFI_EVT_GOT_IP, NULL);
        break;
    case NET_WIFI_MSG_RETRY_TIMER:
        handle_retry_timer(w);
        break;
    case NET_WIFI_MSG_PROV_CREDENTIALS_RESULT:
        handle_credentials_result(w, msg->ok);
        break;
    default:
        break;
    }
}

static void poll_time_topic(net_wifi_t *w)
{
    event_t evt;
    while (core_eventbus_poll(w->bus, w->time_sub, &evt)) {
        if (evt.topic == TOPIC_TIME && evt.u.time.synced &&
            net_wifi_fsm_current_state(&w->fsm) == NET_WIFI_STATE_SYNCING) {
            net_wifi_fsm_dispatch(&w->fsm, NET_WIFI_EVT_SNTP_OK, NULL);
        }
    }
}

/* --- esp_wifi/esp_netif event handler: enqueue only, never dispatch --- */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    net_wifi_t *w = (net_wifi_t *)arg;
    (void)data;

    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_CONNECTED) {
            net_wifi_post_msg(w, NET_WIFI_MSG_STA_CONNECTED, true);
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            net_wifi_post_msg(w, NET_WIFI_MSG_STA_DISCONNECTED, false);
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            net_wifi_post_msg(w, NET_WIFI_MSG_GOT_IP, true);
        }
    }
}

/* --- net_sup task --- */

static void net_sup_task(void *arg)
{
    net_wifi_t *w = (net_wifi_t *)arg;

    char ssid[NET_WIFI_SSID_MAX];
    char psk[NET_WIFI_PSK_MAX];
    if (net_wifi_credentials_load(&w->secure_kv, ssid, psk)) {
        strncpy(w->sta_ssid, ssid, sizeof(w->sta_ssid) - 1);
        strncpy(w->sta_psk, psk, sizeof(w->sta_psk) - 1);
        net_wifi_fsm_dispatch(&w->fsm, NET_WIFI_EVT_HAVE_CREDENTIALS, NULL);
    } else {
        net_wifi_fsm_dispatch(&w->fsm, NET_WIFI_EVT_NO_CREDENTIALS, NULL);
    }

    for (;;) {
        net_wifi_msg_t msg;
        if (xQueueReceive(w->msg_queue, &msg, pdMS_TO_TICKS(500)) == pdTRUE) {
            handle_msg(w, &msg);
        }
        poll_time_topic(w);
    }
}

/* --- construction --- */

bool net_wifi_create(net_wifi_t *w, const net_wifi_cfg_t *cfg)
{
    memset(w, 0, sizeof(*w));
    w->secure_kv      = cfg->secure_kv;
    w->bus            = cfg->bus;
    w->state          = cfg->state;
    w->clock          = cfg->clock;
    w->ap_ssid_prefix = cfg->ap_ssid_prefix;
    w->dns_socket     = -1;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return false;
    }

    w->sta_netif = esp_netif_create_default_wifi_sta();
    w->ap_netif  = esp_netif_create_default_wifi_ap();
    if (w->sta_netif == NULL || w->ap_netif == NULL) {
        ESP_LOGE(TAG, "failed to create default netifs");
        return false;
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wifi_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed");
        return false;
    }
    if (esp_wifi_get_mac(WIFI_IF_STA, w->mac) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_get_mac failed");
        return false;
    }

    /* Keeps svc_storage the sole owner of credentials -- the WiFi driver's
     * own default (WIFI_STORAGE_FLASH) would otherwise persist SSID/PSK
     * into its own NVS blob, a second, ungoverned copy factory reset
     * wouldn't zeroise (SECURITY.md §6, MODULES.md's "only svc_storage
     * touches NVS"). */
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_handler, w, NULL) != ESP_OK ||
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              wifi_event_handler, w, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "failed to register WiFi/IP event handlers");
        return false;
    }

    net_wifi_fsm_actions_t fsm_actions = {
        .start_provisioning = net_wifi_act_start_provisioning,
        .start_connect      = net_wifi_act_start_connect,
        .enter_syncing      = net_wifi_act_enter_syncing,
        .enter_online       = net_wifi_act_enter_online,
        .enter_degraded     = net_wifi_act_enter_degraded,
        .start_backoff      = net_wifi_act_start_backoff,
        .ctx                = w,
    };
    net_wifi_fsm_init(&w->fsm, &fsm_actions, net_wifi_fsm_transition_logger, NULL);

    net_wifi_prov_actions_t prov_actions = {
        .start_ap    = net_wifi_act_prov_start_ap,
        .ap_active   = net_wifi_act_prov_ap_active,
        .begin_save  = net_wifi_act_prov_begin_save,
        .save_failed = net_wifi_act_prov_save_failed,
        .stop_ap     = net_wifi_act_prov_stop_ap,
        .done        = net_wifi_act_prov_done,
        .failed      = net_wifi_act_prov_failed,
        .ctx         = w,
    };
    net_wifi_prov_fsm_init(&w->prov_fsm, &prov_actions, net_wifi_fsm_transition_logger, NULL);

    net_wifi_backoff_init(&w->backoff, rand_u32_wrapper, NULL);

    w->msg_queue = xQueueCreate(NET_WIFI_MSG_QUEUE_LEN, sizeof(net_wifi_msg_t));
    if (w->msg_queue == NULL) {
        ESP_LOGE(TAG, "failed to create message queue");
        return false;
    }

    w->retry_timer = xTimerCreate("net_wifi_retry", pdMS_TO_TICKS(1000), pdFALSE, w, retry_timer_cb);
    if (w->retry_timer == NULL) {
        ESP_LOGE(TAG, "failed to create retry timer");
        return false;
    }

    if (!core_eventbus_subscribe(w->bus, TOPIC_TIME, w->time_sub_storage,
                                   sizeof(w->time_sub_storage) / sizeof(w->time_sub_storage[0]),
                                   &w->time_sub)) {
        ESP_LOGE(TAG, "failed to subscribe to TOPIC_TIME (no policy registered?)");
        return false;
    }

    /* PSRAM-backed stack (MEMORY.md §4's real-hardware finding, found while
     * debugging real artwork/playback TLS fetch failures on the actual
     * Spotify CDN, not just cited): internal SRAM's real usable ceiling is
     * ~138 KiB (idf.py size's own DIRAM total, not MEMORY.md's original
     * ~512 KiB figure), and mbedTLS's dynamic buffer resize needs ~16.7 KiB
     * per session against real (non-fragment-length-negotiating) hosts --
     * every task's stack this project can afford to move off that budget
     * is real margin, same pattern already proven safe for ui_app's own
     * stack. TCB itself still lands in internal RAM automatically
     * (idf_additions.h's own guarantee), only the stack moves. */
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(net_sup_task, "net_sup", NET_WIFI_TASK_STACK,
                                                       w, NET_WIFI_TASK_PRIO, &w->task, 0,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS || w->task == NULL) {
        ESP_LOGE(TAG, "failed to create net_sup task");
        return false;
    }

    return true;
}
