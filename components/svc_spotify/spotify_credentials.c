#include "spotify_credentials.h"

#include <string.h>

#define SPOTIFY_KEY_REFRESH_TOK "sp.refresh_tok"   /* 14 chars */
#define SPOTIFY_KEY_PAIRED_AT   "sp.paired_at"     /* 12 chars */
#define SPOTIFY_KEY_CLIENT_ID   "sp.client_id"     /* 12 chars */
#define SPOTIFY_KEY_REDIRECT    "sp.redirect"      /* 11 chars */
#define SPOTIFY_KEY_AVR_DEVICE  "sp.avr_device"    /* 13 chars */
#define SPOTIFY_KEY_AVR_NAME    "sp.avr_name"      /* 11 chars */

bool spotify_credentials_load(kv_store_if_t *kv, spotify_session_credentials_t *out)
{
    char    refresh_token[SPOTIFY_REFRESH_TOKEN_MAX];
    int64_t paired_at_epoch_s;

    size_t refresh_len = sizeof(refresh_token);
    if (!kv->get_blob(kv->ctx, SPOTIFY_KEY_REFRESH_TOK, refresh_token, &refresh_len) ||
        refresh_len != sizeof(refresh_token)) {
        return false;
    }

    size_t paired_len = sizeof(paired_at_epoch_s);
    if (!kv->get_blob(kv->ctx, SPOTIFY_KEY_PAIRED_AT, &paired_at_epoch_s, &paired_len) ||
        paired_len != sizeof(paired_at_epoch_s)) {
        return false;
    }

    memcpy(out->refresh_token, refresh_token, sizeof(refresh_token));
    out->paired_at_epoch_s = paired_at_epoch_s;
    return true;
}

bool spotify_credentials_save(kv_store_if_t *kv, const spotify_session_credentials_t *in)
{
    char refresh_buf[SPOTIFY_REFRESH_TOKEN_MAX] = { 0 };
    strncpy(refresh_buf, in->refresh_token, SPOTIFY_REFRESH_TOKEN_MAX - 1);

    if (!kv->set_blob(kv->ctx, SPOTIFY_KEY_REFRESH_TOK, refresh_buf, sizeof(refresh_buf))) {
        return false;
    }
    if (!kv->set_blob(kv->ctx, SPOTIFY_KEY_PAIRED_AT, &in->paired_at_epoch_s,
                        sizeof(in->paired_at_epoch_s))) {
        return false;
    }
    return kv->commit(kv->ctx);
}

bool spotify_credentials_clear(kv_store_if_t *kv)
{
    kv->erase_key(kv->ctx, SPOTIFY_KEY_REFRESH_TOK);
    kv->erase_key(kv->ctx, SPOTIFY_KEY_PAIRED_AT);
    return kv->commit(kv->ctx);
}

bool spotify_client_id_load(kv_store_if_t *kv, char out[SPOTIFY_CLIENT_ID_MAX])
{
    size_t len = SPOTIFY_CLIENT_ID_MAX;
    if (!kv->get_blob(kv->ctx, SPOTIFY_KEY_CLIENT_ID, out, &len) || len != SPOTIFY_CLIENT_ID_MAX) {
        return false;
    }
    return true;
}

bool spotify_client_id_save(kv_store_if_t *kv, const char *client_id)
{
    char buf[SPOTIFY_CLIENT_ID_MAX] = { 0 };
    strncpy(buf, client_id, SPOTIFY_CLIENT_ID_MAX - 1);
    if (!kv->set_blob(kv->ctx, SPOTIFY_KEY_CLIENT_ID, buf, sizeof(buf))) {
        return false;
    }
    return kv->commit(kv->ctx);
}

bool spotify_redirect_uri_load(kv_store_if_t *kv, char out[SPOTIFY_REDIRECT_URI_MAX])
{
    size_t len = SPOTIFY_REDIRECT_URI_MAX;
    if (!kv->get_blob(kv->ctx, SPOTIFY_KEY_REDIRECT, out, &len) || len != SPOTIFY_REDIRECT_URI_MAX) {
        return false;
    }
    return true;
}

bool spotify_redirect_uri_save(kv_store_if_t *kv, const char *redirect_uri)
{
    char buf[SPOTIFY_REDIRECT_URI_MAX] = { 0 };
    strncpy(buf, redirect_uri, SPOTIFY_REDIRECT_URI_MAX - 1);
    if (!kv->set_blob(kv->ctx, SPOTIFY_KEY_REDIRECT, buf, sizeof(buf))) {
        return false;
    }
    return kv->commit(kv->ctx);
}

bool spotify_avr_pin_load(kv_store_if_t *kv, spotify_avr_pin_t *out)
{
    char id[SPOTIFY_AVR_DEVICE_ID_MAX];
    char name[SPOTIFY_AVR_NAME_MAX];

    size_t id_len = sizeof(id);
    if (!kv->get_blob(kv->ctx, SPOTIFY_KEY_AVR_DEVICE, id, &id_len) || id_len != sizeof(id)) {
        return false;
    }
    size_t name_len = sizeof(name);
    if (!kv->get_blob(kv->ctx, SPOTIFY_KEY_AVR_NAME, name, &name_len) || name_len != sizeof(name)) {
        return false;
    }

    memcpy(out->avr_device_id, id, sizeof(id));
    memcpy(out->avr_name, name, sizeof(name));
    return true;
}

bool spotify_avr_pin_save(kv_store_if_t *kv, const spotify_avr_pin_t *in)
{
    char id_buf[SPOTIFY_AVR_DEVICE_ID_MAX] = { 0 };
    char name_buf[SPOTIFY_AVR_NAME_MAX]    = { 0 };
    strncpy(id_buf, in->avr_device_id, SPOTIFY_AVR_DEVICE_ID_MAX - 1);
    strncpy(name_buf, in->avr_name, SPOTIFY_AVR_NAME_MAX - 1);

    if (!kv->set_blob(kv->ctx, SPOTIFY_KEY_AVR_DEVICE, id_buf, sizeof(id_buf))) {
        return false;
    }
    if (!kv->set_blob(kv->ctx, SPOTIFY_KEY_AVR_NAME, name_buf, sizeof(name_buf))) {
        return false;
    }
    return kv->commit(kv->ctx);
}

bool spotify_avr_pin_clear(kv_store_if_t *kv)
{
    kv->erase_key(kv->ctx, SPOTIFY_KEY_AVR_DEVICE);
    kv->erase_key(kv->ctx, SPOTIFY_KEY_AVR_NAME);
    return kv->commit(kv->ctx);
}
