/*
 * Event payloads: services -> UI, and service -> service. docs/EVENTS_AND_STATE.md §5.
 *
 * Every event is a fixed-size POD, copied by value -- no allocation, no shared
 * lifetime (§5.2). Large payloads (album art) are never copied; the event
 * carries a generation-tagged handle into a slot pool instead, so a stale
 * handle is a returned NULL rather than a corrupted frame.
 *
 * Payload structs here are intentionally minimal for Phase 1: only
 * setting_evt_t (svc_settings) and diag_evt_t (svc_diagnostics) are actually
 * published this phase. The rest are sized scaffolds matching each topic's
 * one-line description in EVENTS_AND_STATE.md §5.4, grown additively by
 * their owning phase (§5.2b: new fields append with a zero-means-absent
 * value; nothing is ever removed or retyped in place -- that needs a new
 * topic instead).
 */
#ifndef CORE_EVENT_TYPES_H
#define CORE_EVENT_TYPES_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_SCHEMA_VERSION 1   /* bump only on a breaking change -- §5.2b */

typedef enum {
    TOPIC_NET = 0,
    TOPIC_TIME,
    TOPIC_PLAYBACK,
    TOPIC_SPOTIFY,
    TOPIC_DENON,
    TOPIC_ARTWORK,
    TOPIC_SETTINGS,
    TOPIC_POWER,
    TOPIC_OTA,
    TOPIC_TOAST,
    TOPIC_DIAG,
    TOPIC_COUNT,             /* not a real topic -- array-sizing sentinel */
} event_topic_t;

/* --- TOPIC_NET: link up/down, IP acquired, RSSI, provisioning state --- */
typedef enum {
    NET_EVT_LINK_DOWN = 0,
    NET_EVT_LINK_UP,
    NET_EVT_IP_ACQUIRED,
    NET_EVT_RSSI_UPDATED,
    NET_EVT_PROVISIONING_STATE,
} net_evt_kind_t;

typedef struct {
    net_evt_kind_t kind;
    uint32_t       ipv4;          /* network byte order; 0 if not applicable */
    int8_t         rssi_dbm;
    uint8_t        provisioning_state;
} net_evt_t;

/* --- TOPIC_TIME: SNTP synced (gates TLS) --- */
typedef struct {
    bool synced;
} time_evt_t;

/* --- TOPIC_PLAYBACK: track changed, position, play state, shuffle, repeat, device ---
 * Only the change notification travels here; the actual track (title,
 * artist, ...) lives in the playback_state_t domain in core_state, read by
 * whoever needs it. */
typedef enum {
    PLAYBACK_EVT_TRACK_CHANGED = 0,
    PLAYBACK_EVT_POSITION,
    PLAYBACK_EVT_PLAY_STATE,
    PLAYBACK_EVT_SHUFFLE_REPEAT,
    PLAYBACK_EVT_DEVICE_CHANGED,
} playback_evt_kind_t;

typedef struct {
    playback_evt_kind_t kind;
    uint32_t             position_ms;
    bool                 is_playing;
    bool                 shuffle;
    uint8_t              repeat_mode;
} playback_evt_t;

/* --- TOPIC_SPOTIFY: session/auth state, API error, rate-limited --- */
typedef enum {
    SPOTIFY_EVT_SESSION_STATE = 0,
    SPOTIFY_EVT_API_ERROR,
    SPOTIFY_EVT_RATE_LIMITED,
} spotify_evt_kind_t;

typedef struct {
    spotify_evt_kind_t kind;
    int32_t            error_code;
    uint32_t           retry_after_ms;
} spotify_evt_t;

/* --- TOPIC_DENON: power, volume, mute, input, listening mode, link state --- */
typedef enum {
    DENON_EVT_POWER = 0,
    DENON_EVT_VOLUME,
    DENON_EVT_MUTE,
    DENON_EVT_INPUT,
    DENON_EVT_LISTENING_MODE,
    DENON_EVT_LINK_STATE,
} denon_evt_kind_t;

typedef struct {
    denon_evt_kind_t kind;
    uint16_t         volume_half_db;   /* denon_codec.h half-dB representation */
    bool             power_on;
    bool             muted;
    uint8_t          input_id;
} denon_evt_t;

/* --- TOPIC_ARTWORK: artwork ready (handle + palette), artwork failed --- */
typedef struct {
    uint32_t slot;
    uint32_t generation;
} artwork_handle_t;

typedef enum {
    ARTWORK_EVT_READY = 0,
    ARTWORK_EVT_FAILED,
} artwork_evt_kind_t;

typedef struct {
    artwork_evt_kind_t kind;
    artwork_handle_t   handle;
    uint32_t           dominant_color_rgb565;
} artwork_evt_t;

/* --- TOPIC_SETTINGS: setting changed --- (real, published in Phase 1)
 * Carries only the key, not the new value: consumers re-read via
 * svc_settings' typed getters. core_types cannot depend on svc_settings
 * (core -> svc is a reversed layer edge), so the key stays a raw integer
 * here; svc_settings casts to/from its own setting_key_t. */
typedef struct {
    uint16_t key;
} setting_evt_t;

/* --- TOPIC_POWER: active / dim / asleep, backlight level --- */
typedef enum {
    POWER_EVT_STATE_CHANGED = 0,
    POWER_EVT_BACKLIGHT_CHANGED,
} power_evt_kind_t;

typedef struct {
    power_evt_kind_t kind;
    uint8_t          power_state;
    uint8_t          backlight_pct;
} power_evt_t;

/* --- TOPIC_OTA: available, progress, applied, failed --- */
typedef enum {
    OTA_EVT_AVAILABLE = 0,
    OTA_EVT_PROGRESS,
    OTA_EVT_APPLIED,
    OTA_EVT_FAILED,
} ota_evt_kind_t;

typedef struct {
    ota_evt_kind_t kind;
    uint8_t        progress_pct;
    int32_t        error_code;
} ota_evt_t;

/* --- TOPIC_TOAST: transient user-facing message --- */
#define TOAST_MSG_MAX 32

typedef struct {
    char message[TOAST_MSG_MAX];
} toast_evt_t;

/* --- TOPIC_DIAG: fault recorded --- (real, published in Phase 1)
 * Signal only -- the full fault record (task, PC, reason, uptime, per
 * LIFECYCLE.md §2.1) lives in svc_diagnostics' rolling fault log, fetched by
 * index. Keeping detail out of the event is the same pattern as artwork
 * (handle, not pixels) and playback (kind, not the whole track). */
typedef struct {
    uint32_t fault_id;
} diag_evt_t;

typedef struct {
    event_topic_t topic;
    uint32_t      seq;
    int64_t       ts_us;
    union {
        net_evt_t      net;
        time_evt_t     time;
        playback_evt_t playback;
        spotify_evt_t  spotify;
        denon_evt_t    denon;
        artwork_evt_t  artwork;
        setting_evt_t  setting;
        power_evt_t    power;
        ota_evt_t      ota;
        toast_evt_t    toast;
        diag_evt_t     diag;
    } u;
} event_t;

static_assert(sizeof(event_t) <= 64, "event_t grew past the 64 B budget -- "
              "see EVENTS_AND_STATE.md §5.2");

typedef enum {
    EVENT_OVERFLOW_REPLACE_OLDEST = 0,
    EVENT_OVERFLOW_DROP_NEWEST,
} event_overflow_policy_t;

/* Invoked by the bus whenever it discards an event without delivering it --
 * on overflow, on unsubscribe with events still queued, on shutdown with a
 * non-empty queue. The bus does not know why, or what the payload owns;
 * whoever owns the payload owns its release (§5.3). NULL means the payload
 * owns nothing. */
typedef void (*event_release_fn)(const event_t *dropped, void *release_ctx);

typedef struct {
    event_topic_t            topic;
    uint8_t                  depth;
    event_overflow_policy_t  on_full;
    event_release_fn         on_release;   /* nullable */
    void                    *release_ctx;
} topic_policy_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_EVENT_TYPES_H */
