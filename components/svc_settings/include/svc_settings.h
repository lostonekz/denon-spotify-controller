/*
 * Typed get/set over a versioned settings schema, with a RAM shadow and
 * write-coalescing (docs/PLATFORM.md §3.3, docs/MODULES.md). Keys are an
 * enum, not strings, at call sites -- a typo is a compile error.
 *
 * Deliberately has NO dependency on svc_scheduler or svc_storage: the svc
 * layer has no self-edge (arch_rules.json), so cross-service calls route
 * either through a core_types-declared interface (kv_store_if_t, for
 * storage) or, for scheduling, through a plain "tick" function that
 * whoever wires this component up (app_wiring, which sees everything)
 * drives periodically via svc_scheduler -- see svc_settings_tick() below.
 */
#ifndef SVC_SETTINGS_H
#define SVC_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "core_eventbus.h"
#include "kv_store_if.h"
#include "lifecycle_types.h"
#include "os_if_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* docs/PLATFORM.md §3.4 settings inventory. schema_version is NOT here --
 * it's meta about the schema itself, owned directly by
 * svc_settings_migration.c, not a user-facing setting. */
typedef enum {
    SETTING_BRIGHTNESS_ACTIVE = 0,
    SETTING_BRIGHTNESS_DIM,
    SETTING_DIM_TIMEOUT_S,
    SETTING_SLEEP_TIMEOUT_S,
    SETTING_NIGHT_MODE_ENABLED,
    SETTING_NIGHT_START,           /* minutes since midnight */
    SETTING_NIGHT_END,             /* minutes since midnight */
    SETTING_WAKE_ON_PLAYBACK,
    SETTING_DENON_HOST,
    SETTING_DENON_TRANSPORT_PREF,  /* denon_transport_pref_t, stored as u8 */
    SETTING_VOLUME_STEP,
    SETTING_VOLUME_LIMIT_MAX,
    SETTING_DEFAULT_SCREEN,        /* 0 = now_playing (the only screen Phase 1 has) */
    SETTING_OTA_CHANNEL,           /* ota_channel_t, stored as u8 */
    SETTING_OTA_AUTO_APPLY,
    SETTING_COUNT,
} setting_key_t;

typedef enum {
    DENON_TRANSPORT_TELNET = 0,
    DENON_TRANSPORT_HTTP,
    DENON_TRANSPORT_AUTO,
} denon_transport_pref_t;

typedef enum {
    OTA_CHANNEL_STABLE = 0,
    OTA_CHANNEL_BETA,
} ota_channel_t;

#define SETTING_STR_MAX 32

#define SETTINGS_QUIESCE_US   ((int64_t)2  * 1000000)
#define SETTINGS_MAX_DIRTY_US ((int64_t)30 * 1000000)

typedef union {
    uint8_t  u8;
    uint16_t u16;
    bool     b;
    char     str[SETTING_STR_MAX];
} setting_value_t;

typedef struct {
    setting_value_t shadow[SETTING_COUNT];
    bool             key_dirty[SETTING_COUNT];
    bool             any_dirty;
    int64_t          first_dirty_us;
    int64_t          last_dirty_us;

    kv_store_if_t     kv;           /* already open on the "settings" namespace */
    core_eventbus_t  *bus;
    time_source_t      clock;
    SemaphoreHandle_t   lock_sem;

    uint32_t           flush_count;  /* lifetime NVS write count, read by svc_diagnostics */
} svc_settings_t;

/* Runs the schema migration, then loads every setting from kv (falling
 * back to its compiled-in default on a missing key, a size mismatch, or a
 * detected schema rollback -- never misreads a newer format as today's). */
bool svc_settings_create(svc_settings_t *s, kv_store_if_t kv, core_eventbus_t *bus,
                           time_source_t clock);

bool settings_get_u8(svc_settings_t *s, setting_key_t key, uint8_t *out);
bool settings_set_u8(svc_settings_t *s, setting_key_t key, uint8_t value);
bool settings_get_u16(svc_settings_t *s, setting_key_t key, uint16_t *out);
bool settings_set_u16(svc_settings_t *s, setting_key_t key, uint16_t value);
bool settings_get_bool(svc_settings_t *s, setting_key_t key, bool *out);
bool settings_set_bool(svc_settings_t *s, setting_key_t key, bool value);
/* out must have room for SETTING_STR_MAX bytes. */
bool settings_get_str(svc_settings_t *s, setting_key_t key, char *out);
bool settings_set_str(svc_settings_t *s, setting_key_t key, const char *value);

/* Call periodically (recommended: every ~500 ms-1 s), e.g. from a
 * svc_scheduler periodic job posted by app_wiring. Flushes the RAM shadow
 * to NVS once 2 s have passed since the last change, or 30 s since the
 * first unflushed change, whichever comes first. */
void svc_settings_tick(svc_settings_t *s);

/* Unconditional flush, bypassing the debounce window -- for shutdown/OTA
 * hooks. No-op if nothing is dirty. */
void svc_settings_flush_now(svc_settings_t *s);

uint32_t svc_settings_flush_count(const svc_settings_t *s);

/* Not in LIFECYCLE.md §1.2's restart table -- no teardown/restart_fn
 * exists for its RAM shadow yet, so this defaults to the conservative
 * choice. requires storage: settings' kv_store_if_t comes from a
 * namespace svc_storage opens. degraded_ok = true: settings falling back
 * to defaults is a degraded but working state, not fatal. */
static const service_desc_t SVC_SETTINGS_SERVICE_DESC = {
    .id           = SVC_ID_SETTINGS,
    .name         = "settings",
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

#endif /* SVC_SETTINGS_H */
