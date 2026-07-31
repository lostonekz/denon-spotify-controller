#include "ota_manifest.h"

#include <string.h>

static bool put_u8(uint8_t *out, size_t out_cap, size_t *pos, uint8_t v)
{
    if (*pos + 1 > out_cap) {
        return false;
    }
    out[*pos] = v;
    *pos += 1;
    return true;
}

static bool put_u16_be(uint8_t *out, size_t out_cap, size_t *pos, uint16_t v)
{
    if (*pos + 2 > out_cap) {
        return false;
    }
    out[*pos + 0] = (uint8_t)((v >> 8) & 0xFF);
    out[*pos + 1] = (uint8_t)(v & 0xFF);
    *pos += 2;
    return true;
}

static bool put_u32_be(uint8_t *out, size_t out_cap, size_t *pos, uint32_t v)
{
    if (*pos + 4 > out_cap) {
        return false;
    }
    out[*pos + 0] = (uint8_t)((v >> 24) & 0xFF);
    out[*pos + 1] = (uint8_t)((v >> 16) & 0xFF);
    out[*pos + 2] = (uint8_t)((v >> 8) & 0xFF);
    out[*pos + 3] = (uint8_t)(v & 0xFF);
    *pos += 4;
    return true;
}

static bool put_bytes(uint8_t *out, size_t out_cap, size_t *pos, const void *data, size_t len)
{
    if (*pos + len > out_cap) {
        return false;
    }
    memcpy(&out[*pos], data, len);
    *pos += len;
    return true;
}

static bool put_str8(uint8_t *out, size_t out_cap, size_t *pos, const char *s)
{
    size_t len = strlen(s);
    if (len > 0xFF) {
        return false;
    }
    return put_u8(out, out_cap, pos, (uint8_t)len) &&
           put_bytes(out, out_cap, pos, s, len);
}

static bool put_str16(uint8_t *out, size_t out_cap, size_t *pos, const char *s)
{
    size_t len = strlen(s);
    if (len > 0xFFFF) {
        return false;
    }
    return put_u16_be(out, out_cap, pos, (uint16_t)len) &&
           put_bytes(out, out_cap, pos, s, len);
}

bool ota_manifest_canonical_bytes(const ota_manifest_full_t *m,
                                    uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t pos = 0;

    if (!put_str8(out, out_cap, &pos, m->version)) return false;
    if (!put_u32_be(out, out_cap, &pos, m->build)) return false;
    if (!put_u32_be(out, out_cap, &pos, m->security_version)) return false;
    if (!put_str16(out, out_cap, &pos, m->url)) return false;
    if (!put_bytes(out, out_cap, &pos, m->sha256, OTA_SHA256_LEN)) return false;
    if (!put_u32_be(out, out_cap, &pos, m->size)) return false;
    if (!put_str8(out, out_cap, &pos, m->key_id)) return false;
    if (!put_u32_be(out, out_cap, &pos, m->min_prev_build)) return false;
    if (!put_u8(out, out_cap, &pos, m->mandatory ? 0x01 : 0x00)) return false;
    if (!put_str16(out, out_cap, &pos, m->notes)) return false;

    *out_len = pos;
    return true;
}

void ota_manifest_project(const ota_manifest_full_t *full, ota_manifest_t *out)
{
    memset(out, 0, sizeof(*out));
    out->build              = full->build;
    out->security_version    = full->security_version;
    out->min_prev_build      = full->min_prev_build;
    out->image_size          = full->size;
    out->has_signature       = full->sig_len > 0;
    memcpy(out->key_id, full->key_id, sizeof(out->key_id));
    memcpy(out->sha256, full->sha256, sizeof(out->sha256));
}
