/*
 * The authoritative state snapshot. docs/EVENTS_AND_STATE.md §6.
 *
 * Each domain has exactly one writer -- the service that owns it -- and
 * carries its own domain_meta_t so the UI can render "stale, dimmed" honestly
 * instead of a confident thirty-second-old number (§6.2). Readers get a
 * memcpy'd snapshot from core_state; nothing here is ever read or written
 * without going through that store.
 *
 * Field sets are Phase 1 scaffolds sized from what's already specified
 * elsewhere (denon_codec.h's half-dB volume representation, the event
 * payloads in event_types.h) and grown by each domain's owning phase.
 */
#ifndef CORE_APP_STATE_TYPES_H
#define CORE_APP_STATE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "event_types.h"   /* artwork_handle_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LINK_STATUS_OK = 0,
    LINK_STATUS_STALE,
    LINK_STATUS_RECONNECTING,
    LINK_STATUS_UNAVAILABLE,
} link_status_t;

typedef struct {
    link_status_t status;
    int64_t       updated_us;   /* monotonic, from the injected time source */
    uint32_t      version;      /* bumped on every write to this domain */
} domain_meta_t;

typedef enum {
    DOMAIN_NET = 0,
    DOMAIN_SPOTIFY,
    DOMAIN_PLAYBACK,
    DOMAIN_ARTWORK,
    DOMAIN_DENON,
    DOMAIN_POWER,
    DOMAIN_OTA,
    DOMAIN_COUNT,           /* not a real domain -- array-sizing sentinel */
} domain_id_t;

typedef struct {
    domain_meta_t meta;
    bool          link_up;
    uint32_t      ipv4;
    int8_t        rssi_dbm;
    uint8_t       provisioning_state;
} net_state_t;

typedef struct {
    domain_meta_t meta;
    bool          authenticated;
    bool          rate_limited;
    int32_t       last_error_code;
} spotify_state_t;

#define PLAYBACK_TITLE_MAX  64
#define PLAYBACK_ARTIST_MAX 64

typedef struct {
    domain_meta_t meta;
    char          title[PLAYBACK_TITLE_MAX];
    char          artist[PLAYBACK_ARTIST_MAX];
    uint32_t      position_ms;
    uint32_t      duration_ms;
    bool          is_playing;
    bool          shuffle;
    uint8_t       repeat_mode;
} playback_state_t;

typedef struct {
    domain_meta_t    meta;
    artwork_handle_t handle;      /* {0,0} means "no artwork" */
    uint32_t         dominant_color_rgb565;
} artwork_state_t;

typedef struct {
    domain_meta_t meta;
    bool          power_on;
    bool          muted;
    uint16_t      volume_half_db;   /* denon_codec.h representation */
    uint8_t       input_id;
    uint8_t       listening_mode_id;
} denon_state_t;

typedef struct {
    domain_meta_t meta;
    uint8_t       power_state;
    uint8_t       backlight_pct;
} power_state_t;

typedef struct {
    domain_meta_t meta;
    bool          update_available;
    uint8_t       progress_pct;
    uint32_t      running_build;
} ota_state_t;

typedef struct {
    net_state_t      net;
    spotify_state_t  spotify;
    playback_state_t playback;
    artwork_state_t  artwork;
    denon_state_t    denon;
    power_state_t    power;
    ota_state_t      ota;
} app_state_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_APP_STATE_TYPES_H */
