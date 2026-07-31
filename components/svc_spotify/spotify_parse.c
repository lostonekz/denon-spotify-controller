/*
 * See spotify_parse.h. The only file in this codebase that references
 * Spotify's vendor field names (docs/RISKS.md R5).
 */
#include "spotify_parse.h"

#include <string.h>

#include "cJSON.h"

static bool is_blank(const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n') {
            return false;
        }
    }
    return true;
}

static void copy_str_field(const cJSON *obj, const char *key, char *out, size_t out_cap)
{
    if (obj == NULL || out == NULL || out_cap == 0) {
        return;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v == NULL || !cJSON_IsString(v) || v->valuestring == NULL) {
        return;   /* leave the caller's existing value untouched -- R5 tolerance */
    }
    strncpy(out, v->valuestring, out_cap - 1);
    out[out_cap - 1] = '\0';
}

static bool get_bool_field(const cJSON *obj, const char *key, bool default_val)
{
    if (obj == NULL) {
        return default_val;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v == NULL || !(cJSON_IsBool(v) || cJSON_IsTrue(v) || cJSON_IsFalse(v))) {
        return default_val;
    }
    return cJSON_IsTrue(v) ? true : false;
}

static bool get_u32_field(const cJSON *obj, const char *key, uint32_t *out)
{
    if (obj == NULL || out == NULL) {
        return false;
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v == NULL || !cJSON_IsNumber(v) || v->valuedouble < 0) {
        return false;
    }
    *out = (uint32_t)v->valuedouble;
    return true;
}

static spotify_repeat_mode_t parse_repeat_state(const cJSON *root)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "repeat_state");
    if (v == NULL || !cJSON_IsString(v) || v->valuestring == NULL) {
        return SPOTIFY_REPEAT_OFF;
    }
    if (strcmp(v->valuestring, "track") == 0)   return SPOTIFY_REPEAT_TRACK;
    if (strcmp(v->valuestring, "context") == 0) return SPOTIFY_REPEAT_CONTEXT;
    return SPOTIFY_REPEAT_OFF;   /* "off", or an unrecognised future value -- R5 tolerance */
}

static void parse_device(const cJSON *root, playback_state_t *out)
{
    const cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (device == NULL || cJSON_IsNull(device)) {
        return;
    }
    copy_str_field(device, "id", out->device_id, sizeof(out->device_id));
    copy_str_field(device, "name", out->device_name, sizeof(out->device_name));
    out->device_supports_volume = get_bool_field(device, "supports_volume", out->device_supports_volume);
    out->device_is_restricted   = get_bool_field(device, "is_restricted", out->device_is_restricted);
}

static void parse_item(const cJSON *root, playback_state_t *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "item");
    if (item == NULL || cJSON_IsNull(item)) {
        return;
    }
    copy_str_field(item, "id", out->track_id, sizeof(out->track_id));
    copy_str_field(item, "name", out->title, sizeof(out->title));
    get_u32_field(item, "duration_ms", &out->duration_ms);

    const cJSON *artists = cJSON_GetObjectItemCaseSensitive(item, "artists");
    if (cJSON_IsArray(artists) && cJSON_GetArraySize(artists) > 0) {
        copy_str_field(cJSON_GetArrayItem(artists, 0), "name", out->artist, sizeof(out->artist));
    }

    const cJSON *album = cJSON_GetObjectItemCaseSensitive(item, "album");
    if (album == NULL) {
        return;
    }
    copy_str_field(album, "name", out->album, sizeof(out->album));

    const cJSON *images = cJSON_GetObjectItemCaseSensitive(album, "images");
    if (!cJSON_IsArray(images)) {
        return;
    }
    /* Prefer the >=640-wide entry; Spotify's own convention is images[0]
     * being the largest, so fall back to that if none matches exactly. */
    for (int i = 0; i < cJSON_GetArraySize(images); i++) {
        const cJSON *img = cJSON_GetArrayItem(images, i);
        const cJSON *w = cJSON_GetObjectItemCaseSensitive(img, "width");
        if (cJSON_IsNumber(w) && w->valuedouble >= 640) {
            copy_str_field(img, "url", out->artwork_url_640, sizeof(out->artwork_url_640));
            return;
        }
    }
    if (cJSON_GetArraySize(images) > 0) {
        copy_str_field(cJSON_GetArrayItem(images, 0), "url", out->artwork_url_640, sizeof(out->artwork_url_640));
    }
}

spotify_parse_status_t spotify_parse_player_state(const char *json, size_t json_len, playback_state_t *out)
{
    if (out == NULL) {
        return SPOTIFY_PARSE_MALFORMED;
    }
    if (json == NULL || json_len == 0 || is_blank(json, json_len)) {
        return SPOTIFY_PARSE_NO_ACTIVE_DEVICE;   /* 204 / empty body -- valid, not an error */
    }

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return SPOTIFY_PARSE_MALFORMED;
    }

    parse_device(root, out);
    parse_item(root, out);
    get_u32_field(root, "progress_ms", &out->position_ms);
    out->is_playing  = get_bool_field(root, "is_playing", out->is_playing);
    out->shuffle     = get_bool_field(root, "shuffle_state", out->shuffle);
    out->repeat_mode = (uint8_t)parse_repeat_state(root);

    cJSON_Delete(root);
    return SPOTIFY_PARSE_OK;
}

spotify_parse_status_t spotify_parse_device_list(const char *json, size_t json_len, spotify_device_list_t *out)
{
    if (out == NULL) {
        return SPOTIFY_PARSE_MALFORMED;
    }
    if (json == NULL || json_len == 0 || is_blank(json, json_len)) {
        return SPOTIFY_PARSE_NO_ACTIVE_DEVICE;
    }

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return SPOTIFY_PARSE_MALFORMED;
    }

    out->count = 0;
    const cJSON *devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
    if (cJSON_IsArray(devices)) {
        int n = cJSON_GetArraySize(devices);
        for (int i = 0; i < n && out->count < SPOTIFY_PARSE_MAX_DEVICES; i++) {
            const cJSON *d = cJSON_GetArrayItem(devices, i);
            spotify_device_entry_t *e = &out->devices[out->count];
            memset(e, 0, sizeof(*e));
            copy_str_field(d, "id", e->device_id, sizeof(e->device_id));
            copy_str_field(d, "name", e->device_name, sizeof(e->device_name));
            copy_str_field(d, "type", e->device_type, sizeof(e->device_type));
            e->supports_volume = get_bool_field(d, "supports_volume", false);
            e->is_restricted   = get_bool_field(d, "is_restricted", false);
            out->count++;
        }
    }

    cJSON_Delete(root);
    return SPOTIFY_PARSE_OK;
}

spotify_token_parse_result_t spotify_parse_token_response(const char *json, size_t json_len,
                                                              spotify_token_response_t *out)
{
    if (out == NULL || json == NULL || json_len == 0) {
        return SPOTIFY_TOKEN_RESULT_MALFORMED;
    }

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return SPOTIFY_TOKEN_RESULT_MALFORMED;
    }

    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (cJSON_IsString(error) && error->valuestring != NULL) {
        spotify_token_parse_result_t result = (strcmp(error->valuestring, "invalid_grant") == 0)
                                                    ? SPOTIFY_TOKEN_RESULT_INVALID_GRANT
                                                    : SPOTIFY_TOKEN_RESULT_OTHER_ERROR;
        cJSON_Delete(root);
        return result;
    }

    copy_str_field(root, "access_token", out->access_token, sizeof(out->access_token));
    copy_str_field(root, "refresh_token", out->refresh_token, sizeof(out->refresh_token));
    if (out->access_token[0] == '\0') {
        cJSON_Delete(root);
        return SPOTIFY_TOKEN_RESULT_MALFORMED;   /* no error field, but no token either */
    }
    get_u32_field(root, "expires_in", &out->expires_in_s);

    cJSON_Delete(root);
    return SPOTIFY_TOKEN_RESULT_OK;
}
