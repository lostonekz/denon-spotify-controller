/*
 * Entry point.
 *
 * docs/MODULES.md: main.c does nothing but hand off. All wiring lives in the
 * composition root; all policy lives in components.
 *
 * SKELETON SCOPE: during Phase 0 the only thing to hand off to is the
 * bandwidth spike. app_start() and the real composition root arrive in
 * Phase 1, once R1 has an answer and the memory plan is real rather than
 * estimated.
 */

#include "sdkconfig.h"

#include "esp_log.h"

#include "spike_phase0.h"

static const char *TAG = "main";

/* Phase 0 needs a real AP and a real few-megabyte HTTPS URL. Both are
 * environment, not code, so they come from Kconfig rather than being
 * hardcoded and accidentally committed. */
#ifndef CONFIG_DSC_SPIKE_WIFI_SSID
#define CONFIG_DSC_SPIKE_WIFI_SSID ""
#endif
#ifndef CONFIG_DSC_SPIKE_WIFI_PASSWORD
#define CONFIG_DSC_SPIKE_WIFI_PASSWORD ""
#endif
#ifndef CONFIG_DSC_SPIKE_URL
#define CONFIG_DSC_SPIKE_URL "https://www.google.com/"
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "denon-spotify-controller skeleton");
    ESP_LOGI(TAG, "Phase 0: measuring RISKS.md R1 before anything is built "
                  "on top of it");

    spike_config_t cfg = {
        .duration_s       = 120,
        .enable_wifi_load = true,
        .enable_tls_load  = true,
        .enable_jpeg_load = true,
        .enable_nvs_load  = true,
        .download_url     = CONFIG_DSC_SPIKE_URL,
        .wifi_ssid        = CONFIG_DSC_SPIKE_WIFI_SSID,
        .wifi_password    = CONFIG_DSC_SPIKE_WIFI_PASSWORD,
    };

    spike_report_t report;
    spike_phase0_run(&cfg, &report);
    spike_phase0_print(&report);
}
