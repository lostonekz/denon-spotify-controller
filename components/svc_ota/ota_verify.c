#include "ota_verify.h"

#include "esp_image_format.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

bool ota_verify_is_trusted_key(const char *key_id, void *ctx_v)
{
    ota_verify_ctx_t *ctx = (ota_verify_ctx_t *)ctx_v;
    return ota_trust_anchors_is_trusted_key(&ctx->trust, key_id);
}

bool ota_verify_manifest_sig(const ota_manifest_t *m, void *ctx_v)
{
    (void)m;   /* canonical bytes come from ctx->pending_full_manifest -- see ota_verify.h */
    ota_verify_ctx_t *ctx = (ota_verify_ctx_t *)ctx_v;
    if (ctx->pending_full_manifest == NULL) {
        return false;
    }

    uint8_t canonical[OTA_MANIFEST_CANONICAL_MAX];
    size_t canonical_len;
    if (!ota_manifest_canonical_bytes(ctx->pending_full_manifest, canonical, sizeof(canonical), &canonical_len)) {
        return false;
    }

    uint8_t hash[32];
    if (mbedtls_sha256(canonical, canonical_len, hash, 0) != 0) {
        return false;
    }

    const ota_trust_anchor_t *anchor = ota_trust_anchors_find(&ctx->trust, ctx->pending_full_manifest->key_id);
    if (anchor == NULL) {
        return false;   /* is_trusted_key already gated this -- defense in depth */
    }

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    bool ok = mbedtls_pk_parse_public_key(&pk, anchor->pubkey_der, anchor->pubkey_der_len) == 0 &&
              mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                                  ctx->pending_full_manifest->sig, ctx->pending_full_manifest->sig_len) == 0;
    mbedtls_pk_free(&pk);
    return ok;
}

bool ota_verify_image_sig(const uint8_t *sha256, void *ctx_v)
{
    (void)sha256;   /* esp_image_verify() re-derives and checks its own digest/signature from flash */
    ota_verify_ctx_t *ctx = (ota_verify_ctx_t *)ctx_v;
    if (ctx->target_partition == NULL) {
        return false;
    }

    esp_partition_pos_t pos = {
        .offset = ctx->target_partition->address,
        .size   = ctx->target_partition->size,
    };
    esp_image_metadata_t metadata;
    return esp_image_verify(ESP_IMAGE_VERIFY, &pos, &metadata) == ESP_OK;
}
