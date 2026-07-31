/*
 * Declares the symbols tools/gen_trust_anchor.py's generated
 * ota_trust_anchor_dev.c defines. Hand-written and stable (unlike the .c
 * file, this doesn't change when the dev key rotates) -- app_wiring.c
 * (Tier 6) references these two symbols directly when building
 * svc_ota_cfg_t.anchors/anchor_count; nothing else should.
 */
#ifndef OTA_TRUST_ANCHOR_DEV_H
#define OTA_TRUST_ANCHOR_DEV_H

#include <stddef.h>

#include "ota_trust_anchors.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const ota_trust_anchor_t DSC_DEV_TRUST_ANCHORS[];
extern const size_t               DSC_DEV_TRUST_ANCHOR_COUNT;

#ifdef __cplusplus
}
#endif

#endif /* OTA_TRUST_ANCHOR_DEV_H */
