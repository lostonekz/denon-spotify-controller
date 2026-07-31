/*
 * net_wifi target glue: the net_sup task (docs/ARCHITECTURE.md §4: core 0,
 * prio 6, 4 KB, event-driven), esp_wifi/esp_netif wiring, and the SoftAP
 * captive portal, all driving the pure net_wifi_fsm_t/net_wifi_prov_fsm_t
 * built in Tier 1.
 *
 * THREAD SAFETY: net_wifi_fsm_t/net_wifi_prov_fsm_t (core_fsm_t underneath)
 * have no internal lock -- they assume single-threaded, serialized dispatch
 * calls, same as every other core_fsm user in this codebase. But this
 * component has at least three independent ESP-IDF-owned callback
 * contexts that all need to drive these FSMs: the WiFi/IP event handler
 * (runs on the default event loop's own task), the backoff retry timer
 * (esp_timer's task), and the HTTP portal's request handlers (the
 * esp_http_server task). None of them dispatch directly. Each only
 * enqueues a small net_wifi_msg_t into net_sup's own queue; the net_sup
 * task is the ONLY thing that ever calls *_fsm_dispatch(), one message at
 * a time. This is the actual mechanism ARCHITECTURE.md §4 means by
 * "event-driven" for this task.
 *
 * The HTTP portal's credential-save handler is the one exception that
 * still needs a synchronous answer (to render the right response page): it
 * calls net_wifi_credentials_save() directly from its own task (safe --
 * NVS is internally synchronized, and kv_store_if_t holds no other shared
 * mutable state), gets `ok` immediately, and separately enqueues the
 * *notification* of that result for net_sup to fold into the FSM
 * asynchronously (tear down the AP, start the STA connection). The HTTP
 * response does not wait for that second part to finish.
 */
#ifndef NET_WIFI_H
#define NET_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_netif.h"

#include "core_eventbus.h"
#include "core_state.h"
#include "kv_store_if.h"
#include "lifecycle_types.h"
#include "net_wifi_ap_password.h"
#include "net_wifi_backoff.h"
#include "net_wifi_credentials.h"
#include "net_wifi_fsm.h"
#include "net_wifi_provisioning_fsm.h"
#include "os_if_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_WIFI_MSG_QUEUE_LEN 8

typedef enum {
    NET_WIFI_MSG_STA_CONNECTED,
    NET_WIFI_MSG_STA_DISCONNECTED,
    NET_WIFI_MSG_GOT_IP,
    NET_WIFI_MSG_SNTP_SYNCED,        /* relayed from a TOPIC_TIME subscription */
    NET_WIFI_MSG_RETRY_TIMER,
    NET_WIFI_MSG_PROV_AP_UP,
    NET_WIFI_MSG_PROV_AP_START_FAILED,
    NET_WIFI_MSG_PROV_CREDENTIALS_RESULT,   /* .ok valid */
    NET_WIFI_MSG_PROV_AP_STOPPED,
} net_wifi_msg_type_t;

typedef struct {
    net_wifi_msg_type_t type;
    bool                 ok;
} net_wifi_msg_t;

typedef struct {
    /* injected */
    kv_store_if_t     secure_kv;   /* "secure" namespace: wifi.ssid/wifi.psk */
    core_eventbus_t  *bus;
    core_state_t      *state;
    time_source_t       clock;
    const char          *ap_ssid_prefix;   /* Kconfig DSC_PROV_AP_SSID_PREFIX */

    /* FSMs */
    net_wifi_fsm_t          fsm;
    net_wifi_prov_fsm_t     prov_fsm;
    net_wifi_backoff_t       backoff;

    /* ESP-IDF handles */
    esp_netif_t       *sta_netif;
    esp_netif_t       *ap_netif;
    TaskHandle_t         task;
    QueueHandle_t         msg_queue;
    TimerHandle_t          retry_timer;
    eventbus_sub_h           time_sub;
    event_t                   time_sub_storage[4];
    bool                        wifi_started;

    /* cached STA credentials, loaded/saved via net_wifi_credentials_*,
     * re-used on every reconnect attempt without re-reading NVS */
    char sta_ssid[NET_WIFI_SSID_MAX];
    char sta_psk[NET_WIFI_PSK_MAX];

    /* set synchronously by whichever action just ran, read by that
     * action's caller to decide the follow-up dispatch -- see net_wifi.h's
     * top comment on why this can't be a return value. */
    bool last_action_ok;

    uint8_t mac[6];   /* fetched once at create(), used to derive the AP SSID/password */

    /* provisioning-only state, valid while the portal is up */
    char   ap_ssid[33];
    char   ap_password[NET_WIFI_AP_PASSWORD_MAX];
    void  *http_server;      /* httpd_handle_t, opaque here to avoid pulling esp_http_server.h into every consumer */
    int    dns_socket;       /* -1 when not running */
    TaskHandle_t dns_task;
} net_wifi_t;

typedef struct {
    kv_store_if_t     secure_kv;
    core_eventbus_t  *bus;
    core_state_t      *state;
    time_source_t       clock;
    const char          *ap_ssid_prefix;
} net_wifi_cfg_t;

bool net_wifi_create(net_wifi_t *w, const net_wifi_cfg_t *cfg);

/* docs/LIFECYCLE.md §1.2: "netif/LWIP teardown is not reliably complete in
 * ESP-IDF" -- reboot, not restartable. degraded_ok = true: no WiFi means
 * DEGRADED/PROVISIONING, not a fatal condition. */
static const service_desc_t SVC_NET_WIFI_SERVICE_DESC = {
    .id           = SVC_ID_NET_WIFI,
    .name         = "net_wifi",
    .requires     = SVC_BIT(SVC_ID_STORAGE),
    .restart      = RESTART_REBOOT,
    .max_restarts = 0,
    .degraded_ok  = true,
    .teardown     = NULL,
    .restart_fn   = NULL,
    .restart_ctx  = NULL,
};

#ifdef __cplusplus
}
#endif

#endif /* NET_WIFI_H */
