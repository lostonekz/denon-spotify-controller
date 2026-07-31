/*
 * JPEG -> RGB565 decode at 1/2 scale, via espressif/esp_jpeg (TJpgDec) --
 * the ESP32-S3 has no hardware JPEG codec (that peripheral is ESP32-P4
 * only). Same real 640x640->320x320 decode measured during the Phase 0
 * spike (docs/RISKS.md R1: avg 660ms / max ~1.8s), re-added here as the
 * real thing rather than a throwaway measurement. Target-only -- no host
 * test path (TJpgDec is a real decoder, not something worth faking).
 */
#ifndef ARTWORK_DECODE_H
#define ARTWORK_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARTWORK_DECODE_OUT_PX 320   /* 640 source -> 1/2 scale */

/* Decodes a JPEG buffer (jpg_data/jpg_len) into out_rgb565, which must be
 * at least ARTWORK_DECODE_OUT_PX * ARTWORK_DECODE_OUT_PX * 2 bytes
 * (caller-owned -- this is svc_artwork's own pool slot, per
 * artwork_cache.h). Returns false on decode failure (corrupt/truncated/
 * oversized-for-buffer JPEG -- a fetch failure, not a crash). */
bool artwork_decode_jpeg_to_rgb565(const uint8_t *jpg_data, size_t jpg_len, uint16_t *out_rgb565);

#ifdef __cplusplus
}
#endif

#endif /* ARTWORK_DECODE_H */
