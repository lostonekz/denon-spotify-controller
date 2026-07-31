#include "artwork_decode.h"

#include "esp_log.h"
#include "jpeg_decoder.h"

static const char *TAG = "artwork_decode";

bool artwork_decode_jpeg_to_rgb565(const uint8_t *jpg_data, size_t jpg_len, uint16_t *out_rgb565)
{
    if (jpg_data == NULL || jpg_len == 0 || out_rgb565 == NULL) {
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
    };

    esp_jpeg_image_output_t out;
    esp_err_t err = esp_jpeg_decode(&cfg, &out);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_jpeg_decode failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}
