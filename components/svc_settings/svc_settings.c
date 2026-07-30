#include "svc_settings.h"

#include <string.h>

#include "svc_settings_migration.h"

typedef enum {
    SETTING_TYPE_U8,
    SETTING_TYPE_U16,
    SETTING_TYPE_BOOL,
    SETTING_TYPE_STR,
} setting_type_t;

typedef struct {
    setting_type_t   type;
    const char      *nvs_key;   /* <= KV_KEY_MAX bytes */
    setting_value_t   default_value;
} setting_desc_t;

static const setting_desc_t g_setting_descs[SETTING_COUNT] = {
    [SETTING_BRIGHTNESS_ACTIVE]    = { SETTING_TYPE_U8,   "bright_active", { .u8  = 85 } },
    [SETTING_BRIGHTNESS_DIM]       = { SETTING_TYPE_U8,   "bright_dim",    { .u8  = 15 } },
    [SETTING_DIM_TIMEOUT_S]        = { SETTING_TYPE_U16,  "dim_timeout_s", { .u16 = 60 } },
    [SETTING_SLEEP_TIMEOUT_S]      = { SETTING_TYPE_U16,  "sleep_timeout", { .u16 = 300 } },
    [SETTING_NIGHT_MODE_ENABLED]   = { SETTING_TYPE_BOOL, "night_enabled", { .b   = false } },
    [SETTING_NIGHT_START]          = { SETTING_TYPE_U16,  "night_start",   { .u16 = 0 } },
    [SETTING_NIGHT_END]            = { SETTING_TYPE_U16,  "night_end",     { .u16 = 0 } },
    [SETTING_WAKE_ON_PLAYBACK]     = { SETTING_TYPE_BOOL, "wake_on_play",  { .b   = true } },
    [SETTING_DENON_HOST]           = { SETTING_TYPE_STR,  "denon_host",    { .str = "" } },
    [SETTING_DENON_TRANSPORT_PREF] = { SETTING_TYPE_U8,   "denon_transp",  { .u8  = DENON_TRANSPORT_AUTO } },
    [SETTING_VOLUME_STEP]          = { SETTING_TYPE_U8,   "volume_step",   { .u8  = 1 } },
    [SETTING_VOLUME_LIMIT_MAX]     = { SETTING_TYPE_U8,   "volume_limit",  { .u8  = 70 } },
    [SETTING_DEFAULT_SCREEN]       = { SETTING_TYPE_U8,   "default_scrn",  { .u8  = 0 } },
    [SETTING_OTA_CHANNEL]          = { SETTING_TYPE_U8,   "ota_channel",   { .u8  = OTA_CHANNEL_STABLE } },
    [SETTING_OTA_AUTO_APPLY]       = { SETTING_TYPE_BOOL, "ota_apply",     { .b   = true } },
};

static size_t setting_type_size(setting_type_t type)
{
    switch (type) {
    case SETTING_TYPE_U8:   return sizeof(uint8_t);
    case SETTING_TYPE_U16:  return sizeof(uint16_t);
    case SETTING_TYPE_BOOL: return sizeof(bool);
    case SETTING_TYPE_STR:  return SETTING_STR_MAX;
    }
    return 0;
}

static void mark_dirty_and_publish(svc_settings_t *s, setting_key_t key)
{
    int64_t now = s->clock.now_us(s->clock.ctx);

    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    if (!s->any_dirty) {
        s->first_dirty_us = now;
    }
    s->last_dirty_us    = now;
    s->any_dirty         = true;
    s->key_dirty[key]    = true;
    xSemaphoreGive(s->lock_sem);

    event_t evt = { .topic = TOPIC_SETTINGS, .u.setting.key = (uint16_t)key };
    core_eventbus_publish(s->bus, &evt);
}

bool svc_settings_create(svc_settings_t *s, kv_store_if_t kv, core_eventbus_t *bus,
                           time_source_t clock)
{
    memset(s, 0, sizeof(*s));
    s->kv    = kv;
    s->bus   = bus;
    s->clock = clock;

    s->lock_sem = xSemaphoreCreateMutex();
    if (s->lock_sem == NULL) {
        return false;
    }

    settings_migrate_result_t mres = settings_migration_run(&s->kv, NULL, 0);

    for (int i = 0; i < SETTING_COUNT; i++) {
        const setting_desc_t *desc = &g_setting_descs[i];
        setting_value_t val = desc->default_value;

        if (mres == SETTINGS_MIGRATE_OK) {
            size_t want = setting_type_size(desc->type);
            size_t len  = want;
            if (!s->kv.get_blob(s->kv.ctx, desc->nvs_key, &val, &len) || len != want) {
                val = desc->default_value;
            }
        }
        /* mres != OK (rollback or a failed step) -- never trust NVS's
         * per-key values in that case; every key falls back to its
         * compiled-in default instead of risking a misread newer format. */

        s->shadow[i] = val;
    }

    return true;
}

bool settings_get_u8(svc_settings_t *s, setting_key_t key, uint8_t *out)
{
    if (key >= SETTING_COUNT || g_setting_descs[key].type != SETTING_TYPE_U8) {
        return false;
    }
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    *out = s->shadow[key].u8;
    xSemaphoreGive(s->lock_sem);
    return true;
}

bool settings_set_u8(svc_settings_t *s, setting_key_t key, uint8_t value)
{
    if (key >= SETTING_COUNT || g_setting_descs[key].type != SETTING_TYPE_U8) {
        return false;
    }
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    s->shadow[key].u8 = value;
    xSemaphoreGive(s->lock_sem);
    mark_dirty_and_publish(s, key);
    return true;
}

bool settings_get_u16(svc_settings_t *s, setting_key_t key, uint16_t *out)
{
    if (key >= SETTING_COUNT || g_setting_descs[key].type != SETTING_TYPE_U16) {
        return false;
    }
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    *out = s->shadow[key].u16;
    xSemaphoreGive(s->lock_sem);
    return true;
}

bool settings_set_u16(svc_settings_t *s, setting_key_t key, uint16_t value)
{
    if (key >= SETTING_COUNT || g_setting_descs[key].type != SETTING_TYPE_U16) {
        return false;
    }
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    s->shadow[key].u16 = value;
    xSemaphoreGive(s->lock_sem);
    mark_dirty_and_publish(s, key);
    return true;
}

bool settings_get_bool(svc_settings_t *s, setting_key_t key, bool *out)
{
    if (key >= SETTING_COUNT || g_setting_descs[key].type != SETTING_TYPE_BOOL) {
        return false;
    }
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    *out = s->shadow[key].b;
    xSemaphoreGive(s->lock_sem);
    return true;
}

bool settings_set_bool(svc_settings_t *s, setting_key_t key, bool value)
{
    if (key >= SETTING_COUNT || g_setting_descs[key].type != SETTING_TYPE_BOOL) {
        return false;
    }
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    s->shadow[key].b = value;
    xSemaphoreGive(s->lock_sem);
    mark_dirty_and_publish(s, key);
    return true;
}

bool settings_get_str(svc_settings_t *s, setting_key_t key, char *out)
{
    if (key >= SETTING_COUNT || g_setting_descs[key].type != SETTING_TYPE_STR) {
        return false;
    }
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    memcpy(out, s->shadow[key].str, SETTING_STR_MAX);
    xSemaphoreGive(s->lock_sem);
    return true;
}

bool settings_set_str(svc_settings_t *s, setting_key_t key, const char *value)
{
    if (key >= SETTING_COUNT || g_setting_descs[key].type != SETTING_TYPE_STR) {
        return false;
    }
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    strncpy(s->shadow[key].str, value, SETTING_STR_MAX - 1);
    s->shadow[key].str[SETTING_STR_MAX - 1] = '\0';
    xSemaphoreGive(s->lock_sem);
    mark_dirty_and_publish(s, key);
    return true;
}

void svc_settings_flush_now(svc_settings_t *s)
{
    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    if (!s->any_dirty) {
        xSemaphoreGive(s->lock_sem);
        return;
    }

    bool to_write[SETTING_COUNT];
    setting_value_t snapshot[SETTING_COUNT];
    memcpy(to_write, s->key_dirty, sizeof(to_write));
    memcpy(snapshot, s->shadow, sizeof(snapshot));
    memset(s->key_dirty, 0, sizeof(s->key_dirty));
    s->any_dirty = false;
    xSemaphoreGive(s->lock_sem);

    for (int i = 0; i < SETTING_COUNT; i++) {
        if (!to_write[i]) {
            continue;
        }
        const setting_desc_t *desc = &g_setting_descs[i];
        s->kv.set_blob(s->kv.ctx, desc->nvs_key, &snapshot[i], setting_type_size(desc->type));
    }
    s->kv.commit(s->kv.ctx);
    s->flush_count++;
}

void svc_settings_tick(svc_settings_t *s)
{
    int64_t now = s->clock.now_us(s->clock.ctx);
    bool should_flush = false;

    xSemaphoreTake(s->lock_sem, portMAX_DELAY);
    if (s->any_dirty) {
        bool quiesced  = (now - s->last_dirty_us)  >= SETTINGS_QUIESCE_US;
        bool maxed_out = (now - s->first_dirty_us) >= SETTINGS_MAX_DIRTY_US;
        should_flush = quiesced || maxed_out;
    }
    xSemaphoreGive(s->lock_sem);

    if (should_flush) {
        svc_settings_flush_now(s);
    }
}

uint32_t svc_settings_flush_count(const svc_settings_t *s)
{
    return s->flush_count;
}
