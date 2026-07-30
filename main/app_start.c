#include "app_start.h"

#include "esp_log.h"
#include "esp_system.h"

#include "app_wiring.h"

static const char *TAG = "app_start";

/* The one static app_ctx_t for the life of the device (ARCHITECTURE.md
 * §6) -- never on the stack, never heap-allocated. */
static app_ctx_t g_ctx;

void app_start(void)
{
    if (app_wiring_create(&g_ctx)) {
        return;
    }

    /* app_wiring failure means something didn't come up cleanly at boot --
     * flash corruption, a task that failed to create, an invalid boot
     * graph. Unlike a runtime service failure (svc_supervisor's job, with
     * its capped 3-attempts-in-10-minutes reboot policy), there is no
     * reliable place to persist an attempt counter here: the very
     * component that might have failed is svc_storage itself. Reboot
     * unconditionally and let the UART log show a human a repeating
     * failure if there is one, rather than risk under-counting attempts
     * against a storage layer that may be the thing that's broken. */
    ESP_LOGE(TAG, "app_wiring_create failed -- rebooting");
    esp_restart();
}
