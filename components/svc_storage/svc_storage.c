#include "svc_storage.h"

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "svc_storage";

static bool kv_get_blob(void *ctx, const char *key, void *out, size_t *inout_len)
{
    svc_storage_ns_slot_t *slot = (svc_storage_ns_slot_t *)ctx;

    esp_err_t err = nvs_get_blob(slot->handle, key, out, inout_len);
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        /* Buffer too small -- report the real size so the caller can retry
         * with one big enough, per this interface's documented contract. */
        size_t actual = 0;
        nvs_get_blob(slot->handle, key, NULL, &actual);
        *inout_len = actual;
        return false;
    }
    return err == ESP_OK;
}

static bool kv_set_blob(void *ctx, const char *key, const void *data, size_t len)
{
    svc_storage_ns_slot_t *slot = (svc_storage_ns_slot_t *)ctx;

    esp_err_t err = nvs_set_blob(slot->handle, key, data, len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_blob(%s) failed: %s", key, esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool kv_erase_key(void *ctx, const char *key)
{
    svc_storage_ns_slot_t *slot = (svc_storage_ns_slot_t *)ctx;
    return nvs_erase_key(slot->handle, key) == ESP_OK;
}

static bool kv_commit(void *ctx)
{
    svc_storage_ns_slot_t *slot = (svc_storage_ns_slot_t *)ctx;

    esp_err_t err = nvs_commit(slot->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "commit failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool svc_storage_create(svc_storage_t *s)
{
    memset(s, 0, sizeof(*s));

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Documented ESP-IDF recovery: erase and retry once. Only ever
         * discards NVS content, never the running image or partition
         * table. */
        ESP_LOGW(TAG, "nvs_flash_init needs erase (%s), retrying", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

bool svc_storage_open_namespace(svc_storage_t *s, const char *namespace_name,
                                 kv_ns_kind_t kind, kv_store_if_t *out)
{
    (void)kind;   /* documented no-op until the production secure-init path exists */

    if (namespace_name == NULL || strlen(namespace_name) > 15) {
        ESP_LOGE(TAG, "namespace name invalid or too long");
        return false;
    }

    int free_slot = -1;
    for (int i = 0; i < SVC_STORAGE_MAX_NAMESPACES; i++) {
        if (!s->namespaces[i].in_use) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) {
        ESP_LOGE(TAG, "namespace table full (%d), cannot open %s",
                 SVC_STORAGE_MAX_NAMESPACES, namespace_name);
        return false;
    }

    svc_storage_ns_slot_t *slot = &s->namespaces[free_slot];
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &slot->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", namespace_name, esp_err_to_name(err));
        return false;
    }
    slot->in_use = true;

    out->get_blob  = kv_get_blob;
    out->set_blob  = kv_set_blob;
    out->erase_key = kv_erase_key;
    out->commit    = kv_commit;
    out->ctx       = slot;

    return true;
}

void svc_storage_destroy(svc_storage_t *s)
{
    for (int i = 0; i < SVC_STORAGE_MAX_NAMESPACES; i++) {
        if (s->namespaces[i].in_use) {
            nvs_close(s->namespaces[i].handle);
            s->namespaces[i].in_use = false;
        }
    }
}
