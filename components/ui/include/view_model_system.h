/*
 * Pure builder for the System screen -- (net_state_t, denon_state_t,
 * spotify_state_t) plus a handful of plain values the target-only apply-
 * glue supplies (firmware version/date, panel identifier, Wi-Fi RSSI,
 * uptime clock) -> system_vm_t. Zero LVGL/ESP-IDF, host-tested.
 *
 * Firmware version/date, panel identifier, and RSSI are read via direct
 * IDF calls in view_model_system_apply.c (esp_ota_get_app_description(),
 * a MAC-based identifier, esp_wifi_sta_get_ap_info()) rather than here --
 * this builder only ever formats plain data it's handed, so it stays
 * host-testable with literal strings/values in tests, same pattern
 * view_model_now_playing_build() already uses for its own injected
 * time_source_t clock.
 */
#ifndef VIEW_MODEL_SYSTEM_H
#define VIEW_MODEL_SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state_types.h"
#include "os_if_types.h"
#include "view_model_tone.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VM_SYSTEM_VERDICT_MAX     32   /* "Everything's working" + NUL, rounded up */
#define VM_SYSTEM_WIFI_MAX        16   /* "-128 dBm" + NUL, or "Disconnected" */
#define VM_SYSTEM_LATENCY_MAX     16   /* "1234 ms" + NUL, or "--" */
#define VM_SYSTEM_SPOTIFY_MAX     16   /* "Reauth needed" + NUL */
#define VM_SYSTEM_FIRMWARE_MAX    64   /* "<version> - <date>" + NUL */
#define VM_SYSTEM_UPTIME_MAX      16   /* "999 d 23 h" + NUL */
#define VM_SYSTEM_PANEL_ID_MAX    32

typedef struct {
    char     verdict[VM_SYSTEM_VERDICT_MAX];

    tone_t   wifi_tone;
    char     wifi_display[VM_SYSTEM_WIFI_MAX];

    tone_t   denon_tone;
    char     denon_display[VM_SYSTEM_LATENCY_MAX];

    tone_t   spotify_tone;
    char     spotify_display[VM_SYSTEM_SPOTIFY_MAX];

    char     firmware_display[VM_SYSTEM_FIRMWARE_MAX];
    char     uptime_display[VM_SYSTEM_UPTIME_MAX];
    char     panel_id[VM_SYSTEM_PANEL_ID_MAX];
} system_vm_t;

system_vm_t view_model_system_build(const net_state_t *net, const denon_state_t *denon,
                                        const spotify_state_t *spotify, int8_t wifi_rssi_dbm,
                                        const char *fw_version, const char *fw_date,
                                        const char *panel_id, time_source_t clock);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_SYSTEM_H */
