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
    DOMAIN_UI,               /* Phase 5: owned/written by `ui` alone -- a legal ui->core
                                * edge. R10 wants a dropped-frame counter always running,
                                * R17 wants svc_diagnostics to count unrepresentable-glyph
                                * events, but `ui` may not depend on any svc_* component
                                * and the bus's documented direction is services->UI, not
                                * the reverse -- this domain plus an app_wiring relay into
                                * svc_diagnostics's periodic report is the mechanism,
                                * mirroring the existing TOPIC_SETTINGS->svc_denon /
                                * TOPIC_OTA->svc_artwork relay pattern. */
    DOMAIN_COUNT,           /* not a real domain -- array-sizing sentinel */
} domain_id_t;

typedef struct {
    domain_meta_t meta;
    bool          link_up;
    uint32_t      ipv4;
    int8_t        rssi_dbm;
    uint8_t       provisioning_state;
} net_state_t;

/* Phase 4: a UI-relevant projection of spotify_session_fsm's own 7-state
 * enum, never the raw FSM state itself -- same pattern ota_state_t already
 * uses for ota_fsm_state_t (svc_ota's own header never touches core_types).
 * The raw spotify_fsm_state_t stays inside svc_spotify's own header. */
typedef enum {
    SPOTIFY_SESSION_UNPAIRED = 0,
    SPOTIFY_SESSION_PAIRING,           /* collapses AWAITING_CODE + EXCHANGING_CODE */
    SPOTIFY_SESSION_ACTIVE,            /* collapses AUTHORIZED + RESUMING */
    SPOTIFY_SESSION_REAUTH_REQUIRED,
    SPOTIFY_SESSION_REFRESHING,        /* System screen's "Renewing" -- a proactive token
                                           refresh is in flight (spotify_session_fsm's own
                                           REFRESHING state); set at action_start_refresh(),
                                           cleared back to ACTIVE/REAUTH_REQUIRED by the same
                                           real transitions that already run today */
} spotify_session_summary_t;

typedef struct {
    domain_meta_t              meta;
    bool                       authenticated;
    bool                       rate_limited;
    int32_t                    last_error_code;
    spotify_session_summary_t  session_status;
    bool                       reauth_recommended;   /* advisory -- refresh token nearing
                                                       * its ~6mo ceiling; session may
                                                       * still be ACTIVE */
} spotify_state_t;

#define PLAYBACK_TITLE_MAX       64
#define PLAYBACK_ARTIST_MAX      64
#define PLAYBACK_ALBUM_MAX       64
#define PLAYBACK_TRACK_ID_MAX    40   /* Spotify base62 ID is 22 chars; headroom for a URI form */
#define PLAYBACK_ARTWORK_URL_MAX 96   /* i.scdn.co URLs observed ~70-90 chars */
#define PLAYBACK_DEVICE_ID_MAX   48
#define PLAYBACK_DEVICE_NAME_MAX 32

/* Phase 4/adr-0007: whether the active Spotify Connect device is the Denon
 * AVR, used by main/volume_coordinator.c to decide where a volume intent
 * goes. Derived by matching against a user-pinned device id/name
 * (spotify_avr_match.c) -- deliberately NOT derived from Spotify's device
 * `type` field, which does not document "AVR" as a real value. UNKNOWN is
 * the safe, zero-value default: "we cannot confidently tell," matching
 * adr/0007's own stated fallback. */
typedef enum {
    DEVICE_IS_AVR_UNKNOWN = 0,
    DEVICE_IS_AVR_YES,
    DEVICE_IS_AVR_NO,
} device_is_avr_t;

typedef struct {
    domain_meta_t   meta;
    char            title[PLAYBACK_TITLE_MAX];
    char            artist[PLAYBACK_ARTIST_MAX];
    char            album[PLAYBACK_ALBUM_MAX];
    char            track_id[PLAYBACK_TRACK_ID_MAX];         /* svc_artwork's re-fetch key */
    char            artwork_url_640[PLAYBACK_ARTWORK_URL_MAX];
    char            device_id[PLAYBACK_DEVICE_ID_MAX];
    char            device_name[PLAYBACK_DEVICE_NAME_MAX];
    device_is_avr_t device_is_avr;
    bool            device_supports_volume;   /* Spotify DeviceObject.supports_volume */
    bool            device_is_restricted;     /* DeviceObject.is_restricted -- true means
                                                * the Web API cannot control this device
                                                * at all, volume or otherwise */
    uint32_t        position_ms;
    uint32_t        duration_ms;
    bool            is_playing;
    bool            shuffle;
    uint8_t         repeat_mode;
} playback_state_t;

typedef struct {
    domain_meta_t    meta;
    artwork_handle_t handle;      /* {0,0} means "no artwork" */
    uint32_t         dominant_color_rgb565;
    char             track_id[PLAYBACK_TRACK_ID_MAX];   /* Phase 5: which track this handle
                                                           * is for, zero-means-absent. svc_artwork
                                                           * already discards a stale in-flight
                                                           * *fetch* at publish time, but a reader
                                                           * across an async boundary (the UI) has
                                                           * no other way to positively confirm
                                                           * "this artwork is for the track I'm
                                                           * currently showing" vs. a second track
                                                           * change racing ahead of render. */
} artwork_state_t;

#define DENON_HOST_MAX 64   /* matches denon_service_state_t.host's own size */

typedef struct {
    domain_meta_t meta;
    bool          power_on;
    bool          muted;
    uint16_t      volume_half_db;   /* denon_codec.h representation */
    uint8_t       input_id;
    uint8_t       listening_mode_id;
    char          host[DENON_HOST_MAX];       /* mirrors denon_service_state_t.host --
                                                    svc_denon already owns this value (set via
                                                    the TOPIC_SETTINGS relay), just never
                                                    mirrored into core_state before the
                                                    Connection screen needed to show it */
    uint16_t      volume_limit_half_db;       /* mirrors denon_service_state_t.
                                                    volume_limit_half_db -- so the Connection
                                                    screen's cap slider can show/seed its
                                                    current value */
    uint32_t      round_trip_ms;              /* an honest approximation -- time from our last
                                                    sent command to the next line received on
                                                    this link, not per-command request/response
                                                    correlation (the protocol has no request
                                                    IDs). 0 until the first real measurement. */
} denon_state_t;

/* The values power_state_t.power_state and power_evt_t.power_state carry.
 * Both stay uint8_t rather than being retyped to this enum: power_evt_t is
 * an event payload inside event_t's fixed 64-byte union, and
 * EVENTS_AND_STATE.md §5.2b's additive-only rule forbids retyping a field
 * in place. Same shape as net_evt_t's own uint8_t provisioning_state.
 *
 * Two states, deliberately -- so §6.4's "anything with more than three
 * states gets a core_fsm table" does NOT apply here, and svc_power stays a
 * threshold comparison rather than a state machine. There is no DIM state
 * because hal_backlight is binary on/off (hal_backlight.h); real
 * percentage dimming needs the CH32V003 PWM path that header documents as
 * possible but unbuilt. backlight_pct is therefore only ever 0 or 100
 * today -- it is not a lie, just a coarse instrument. */
typedef enum {
    POWER_MODE_ACTIVE = 0,
    POWER_MODE_ASLEEP,
} power_mode_t;

typedef struct {
    domain_meta_t meta;
    uint8_t       power_state;     /* a power_mode_t value */
    uint8_t       backlight_pct;
} power_state_t;

typedef struct {
    domain_meta_t meta;
    bool          update_available;
    uint8_t       progress_pct;
    uint32_t      running_build;
} ota_state_t;

/* Phase 5: `ui`'s own diagnostics, relayed by app_wiring into
 * svc_diagnostics's existing periodic UART report (DOMAIN_UI's own top
 * comment explains why this domain exists rather than a direct call).
 * Counters, not rates -- svc_diagnostics already has the cadence to turn a
 * counter into a rate for its report; duplicating that math here would be
 * a second place to get it wrong. */
typedef struct {
    domain_meta_t meta;
    uint32_t      frames_rendered;
    uint32_t      frames_dropped;
    uint32_t      unrepresentable_glyph_count;   /* R17 */
    /* Longest single ui_app_task iteration observed since the previous
     * publish -- a peak, not a counter, so it IS reset each window (unlike
     * the three above). The loop already timed itself for R10's watchdog
     * assertion; this just surfaces the number instead of only comparing it
     * to a 600ms threshold, so a render change that costs 5ms more per
     * frame is visible before it becomes a stall. Added when rounding the
     * slider ends moved them off LVGL's single-blend fast path onto a
     * masked per-row one (w_progress_bar.c) -- the kind of "probably fine"
     * that this project has been wrong about before (RISKS.md R1's 337ms
     * crossfade). */
    uint32_t      max_loop_us;
} ui_diag_state_t;

typedef struct {
    net_state_t      net;
    spotify_state_t  spotify;
    playback_state_t playback;
    artwork_state_t  artwork;
    denon_state_t    denon;
    power_state_t    power;
    ota_state_t      ota;
    ui_diag_state_t  ui;
} app_state_t;

#ifdef __cplusplus
}
#endif

#endif /* CORE_APP_STATE_TYPES_H */
