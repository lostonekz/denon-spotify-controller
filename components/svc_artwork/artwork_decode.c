#include "artwork_decode.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "jpeg_decoder.h"

static const char *TAG = "artwork_decode";

/* MUST mirror jpeg_decoder.c's own JPEG_WORK_BUF_SIZE sizing, which keys off
 * the same JD_FASTDECODE value -- undersizing this would let TJpgDec scribble
 * past the end of a buffer we own. Kept as a parallel #if rather than a fixed
 * number precisely so that flipping the optimization level in menuconfig
 * cannot silently turn it into an overflow. */
#if defined(CONFIG_JD_FASTDECODE) && (CONFIG_JD_FASTDECODE == 2)
#define ARTWORK_JPEG_WORK_BUF_BYTES 65472
#else
#define ARTWORK_JPEG_WORK_BUF_BYTES 3100
#endif

bool artwork_decode_jpeg_to_rgb565(const uint8_t *jpg_data, size_t jpg_len, uint16_t *out_rgb565)
{
    if (jpg_data == NULL || jpg_len == 0 || out_rgb565 == NULL) {
        return false;
    }

    /* Supplied explicitly so it lands in PSRAM. Left NULL, esp_jpeg allocates
     * it itself with MALLOC_CAP_DEFAULT (jpeg_decoder.c) -- and this project
     * sets CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096, so a 3.1 KB request
     * PREFERS internal SRAM. That is ~3.1 KB of the ~24 KB free internal heap
     * (MEMORY.md §1), churned on every track change, for a scratchpad with no
     * reason to be there. TJpgDec never allocates on its own; this is the
     * whole of its working memory. */
    void *workbuf = heap_caps_malloc(ARTWORK_JPEG_WORK_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (workbuf == NULL) {
        ESP_LOGW(TAG, "work buffer alloc failed (%d bytes PSRAM)", ARTWORK_JPEG_WORK_BUF_BYTES);
        return false;
    }

    esp_jpeg_image_cfg_t cfg = {
        .indata      = (uint8_t *)jpg_data,
        .indata_size = jpg_len,
        .outbuf      = (uint8_t *)out_rgb565,
        .outbuf_size = ARTWORK_DECODE_OUT_PX * ARTWORK_DECODE_OUT_PX * sizeof(uint16_t),
        .out_format  = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale   = JPEG_IMAGE_SCALE_1_2,
        .flags       = { .swap_color_bytes = 0 },
        .advanced    = { .working_buffer      = workbuf,
                          .working_buffer_size = ARTWORK_JPEG_WORK_BUF_BYTES },
    };

    esp_jpeg_image_output_t out;
    esp_err_t err = esp_jpeg_decode(&cfg, &out);
    heap_caps_free(workbuf);
    if (err != ESP_OK) {
        /* Worth reading the code, not just the name. ESP_ERR_NOT_SUPPORTED is
         * TJpgDec's JDR_FMT3 -- a JPEG standard it cannot handle (progressive,
         * arithmetic-coded, CMYK, or a chroma subsampling outside 4:4:4 /
         * 4:2:2 / 4:2:0). ESP_FAIL covers JDR_INP, which despite reading like
         * a format problem means the decoder ran off the END of the input
         * buffer -- i.e. a truncated or desynced fetch, not an unsupported
         * image. See artwork_service_fetch.c's own note. */
        ESP_LOGW(TAG, "esp_jpeg_decode failed: %s (len=%u)", esp_err_to_name(err), (unsigned)jpg_len);
        return false;
    }
    return true;
}
