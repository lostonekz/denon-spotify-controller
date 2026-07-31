#include "ota_trust_anchors.h"

#include <string.h>

const ota_trust_anchor_t *ota_trust_anchors_find(const ota_trust_anchors_t *t, const char *key_id)
{
    for (size_t i = 0; i < t->count; i++) {
        if (strcmp(t->anchors[i].key_id, key_id) == 0) {
            return &t->anchors[i];
        }
    }
    return NULL;
}

bool ota_trust_anchors_is_trusted_key(const ota_trust_anchors_t *t, const char *key_id)
{
    return ota_trust_anchors_find(t, key_id) != NULL;
}
