/*
 * Pure builder for the Connection screen -- denon_state_t alone ->
 * connection_vm_t. Zero LVGL/ESP-IDF, host-tested, same Tier-1 shape as
 * view_model_denon_build().
 */
#ifndef VIEW_MODEL_CONNECTION_H
#define VIEW_MODEL_CONNECTION_H

#include <stdbool.h>
#include <stdint.h>

#include "app_state_types.h"
#include "view_model_tone.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VM_CONNECTION_HOST_MAX          DENON_HOST_MAX
#define VM_CONNECTION_ROUND_TRIP_MAX    16   /* "1234 ms" + NUL, or "--" */
#define VM_CONNECTION_LINK_LABEL_MAX    16   /* "Linked"/"Fallback" + NUL */
#define VM_CONNECTION_VOLUME_DISPLAY_MAX 8   /* "98.5" + NUL */

typedef struct {
    char     host[VM_CONNECTION_HOST_MAX];               /* "Not set" if never configured */
    char     round_trip_display[VM_CONNECTION_ROUND_TRIP_MAX];   /* "--" until the first
                                                                       real measurement */
    tone_t   link_tone;
    char     link_label[VM_CONNECTION_LINK_LABEL_MAX];   /* header pill text */
    bool     telnet_selected;   /* link_tone == TONE_LIVE */
    bool     http_selected;     /* !telnet_selected -- always exactly one of the two, since
                                     svc_denon's own writer only ever reports OK or STALE for
                                     this domain today (see denon_service_io.c) */
    uint16_t volume_limit_half_db_raw;
    char     volume_limit_display[VM_CONNECTION_VOLUME_DISPLAY_MAX];
} connection_vm_t;

connection_vm_t view_model_connection_build(const denon_state_t *denon);

#ifdef __cplusplus
}
#endif

#endif /* VIEW_MODEL_CONNECTION_H */
