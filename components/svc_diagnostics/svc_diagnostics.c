#include "svc_diagnostics.h"

#include <inttypes.h>
#include <string.h>

#include "esp_core_dump.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CDUMP_MARKER_KEY "cdump_marker"

static const char *TAG = "svc_diagnostics";

typedef struct {
    bool     valid;
    uint32_t exc_pc;
    char     exc_task[FAULTLOG_TASK_NAME_MAX];
} coredump_marker_t;

bool svc_diagnostics_create(svc_diagnostics_t *d, core_eventbus_t *bus, core_state_t *state,
                              kv_store_if_t kv, time_source_t clock)
{
    memset(d, 0, sizeof(*d));
    d->bus   = bus;
    d->state = state;
    d->kv    = kv;
    d->clock = clock;
    return true;
}

static void publish_diag_event(svc_diagnostics_t *d)
{
    event_t evt = { .topic = TOPIC_DIAG, .u.diag.fault_id = d->fault_seq };
    d->fault_seq++;
    core_eventbus_publish(d->bus, &evt);
}

void svc_diagnostics_record_fault(svc_diagnostics_t *d, const char *task_name, uint32_t reason)
{
    fault_record_t rec = { 0 };
    strncpy(rec.task_name, task_name, FAULTLOG_TASK_NAME_MAX - 1);
    rec.reason         = reason;
    rec.pc             = 0;   /* no crash PC for a manually-recorded fault */
    rec.uptime_s        = (uint32_t)(d->clock.now_us(d->clock.ctx) / 1000000);
    rec.heap_free_min  = esp_get_minimum_free_heap_size();

    faultlog_append(&d->kv, &rec);
    publish_diag_event(d);
}

void svc_diagnostics_check_coredump_on_boot(svc_diagnostics_t *d)
{
    if (esp_core_dump_image_check() != ESP_OK) {
        return;   /* no dump present */
    }

    esp_core_dump_summary_t summary;
    if (esp_core_dump_get_summary(&summary) != ESP_OK) {
        return;
    }

    coredump_marker_t marker;
    size_t len = sizeof(marker);
    bool have_marker = d->kv.get_blob(d->kv.ctx, CDUMP_MARKER_KEY, &marker, &len) &&
                        len == sizeof(marker);

    if (have_marker && marker.valid && marker.exc_pc == summary.exc_pc &&
        strncmp(marker.exc_task, summary.exc_task, FAULTLOG_TASK_NAME_MAX) == 0) {
        return;   /* already recorded on a previous boot */
    }

    ESP_LOGW(TAG, "unretrieved coredump detected: task=%s pc=0x%08" PRIx32,
             summary.exc_task, summary.exc_pc);

    fault_record_t rec = { 0 };
    strncpy(rec.task_name, summary.exc_task, FAULTLOG_TASK_NAME_MAX - 1);
    rec.pc     = summary.exc_pc;
    rec.reason = FAULT_REASON_COREDUMP;
    /* This boot's uptime/heap-min, not the crashed boot's -- ESP-IDF's
     * coredump summary doesn't capture either at panic time. Best-effort,
     * documented, not fabricated precision. */
    rec.uptime_s       = (uint32_t)(d->clock.now_us(d->clock.ctx) / 1000000);
    rec.heap_free_min = esp_get_minimum_free_heap_size();

    faultlog_append(&d->kv, &rec);
    publish_diag_event(d);

    coredump_marker_t new_marker = { .valid = true, .exc_pc = summary.exc_pc };
    strncpy(new_marker.exc_task, summary.exc_task, FAULTLOG_TASK_NAME_MAX - 1);
    d->kv.set_blob(d->kv.ctx, CDUMP_MARKER_KEY, &new_marker, sizeof(new_marker));
    d->kv.commit(d->kv.ctx);
}

/* This project's own task names (confirmed by reading every xTaskCreate*
 * call site directly), not ESP-IDF/lwIP/driver-internal tasks (IDLE0/
 * IDLE1/Tmr Svc/ipc0/ipc1/wifi/sys_evt/tiT/esp_timer/console_repl) -- this
 * diagnostic exists to catch a stack-size budget THIS codebase chose being
 * wrong, not to babysit tasks this codebase doesn't own the sizing of.
 * "ota" deliberately omitted: it only exists during an active OTA download,
 * so resolving its handle once at boot would almost always just fail. */
static const char *const DIAG_TASK_NAMES[] = {
    "ui_app", "svc_scheduler", "artwork", "denon", "spotify", "net_sup", "intent",
};
#define DIAG_TASK_COUNT (sizeof(DIAG_TASK_NAMES) / sizeof(DIAG_TASK_NAMES[0]))

static TaskHandle_t s_diag_task_handles[DIAG_TASK_COUNT];
static bool           s_diag_handles_resolved = false;

/* A real, confirmed finding from real-hardware testing (Phase 5 Tier 7),
 * not a style choice: this used to call uxTaskGetSystemState() to discover
 * and report on every task in the system. Confirmed by reading FreeRTOS-
 * Kernel-SMP/tasks.c directly (this target is dual-core,
 * CONFIG_FREERTOS_UNICORE=n): uxTaskGetSystemState() calls vTaskSuspendAll()
 * for its ENTIRE duration, on BOTH cores, while it walks every task's stack
 * (a real, ~15-task, up-to-12KB-stack scan) to compute each one's high-
 * water mark. A first fix just ran that scan less often (every 12th tick
 * instead of every tick) -- real, measured improvement, but the SAME freeze
 * still recurred every ~60s, because the fix reduced its FREQUENCY, not its
 * root cause. The actual fix: uxTaskGetStackHighWaterMark(xTask) (a single
 * task's handle, not "enumerate everything") does NOT call
 * vTaskSuspendAll() at all -- confirmed by reading the same file, it's a
 * direct, unlocked stack walk. xTaskGetHandle(name) (resolving a name to a
 * handle) still suspends the scheduler, but only for a fast name-string
 * search, not a slow per-task memory scan -- and it only needs to run ONCE
 * per task, not every tick, since a task's handle never changes after
 * creation. Resolved lazily on this component's own first tick (~5s after
 * boot, safely after app_wiring_create() has already created every task
 * this list names, confirmed against real boot logs), cached forever after
 * -- every tick from then on costs zero scheduler-suspend time at all. */
static void resolve_task_handles_once(void)
{
    if (s_diag_handles_resolved) {
        return;
    }
    for (size_t i = 0; i < DIAG_TASK_COUNT; i++) {
        s_diag_task_handles[i] = xTaskGetHandle(DIAG_TASK_NAMES[i]);
    }
    s_diag_handles_resolved = true;
}

void svc_diagnostics_report_tick(svc_diagnostics_t *d)
{
    d->report_count++;

    /* free/min_free (esp_get_free_heap_size()/esp_get_minimum_free_heap_size())
     * are MALLOC_CAP_DEFAULT -- internal SRAM and PSRAM combined. MEMORY.md
     * §1 budgets internal SRAM specifically (the scarce ~512 KB resource,
     * not the 8 MB of PSRAM), so it needs its own number, not a difference
     * of the other two. */
    ESP_LOGI(TAG, "heap: free=%u min_free=%u psram_free=%u internal_free=%u internal_free_min=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

    /* Tier 7: DOMAIN_UI, published by ui_app_task's own loop (a legal
     * ui->core edge) roughly once a second -- read directly here, no
     * app_wiring relay needed (svc_diagnostics already depends on core_state
     * directly, same as every other domain read in this codebase). Reads as
     * all-zero (LINK_STATUS_UNAVAILABLE's default meta) until the UI task's
     * first publish, which is fine -- this report already tolerates a
     * not-yet-created task in the loop below. */
    ui_diag_state_t ui_diag = { 0 };
    core_state_read(d->state, DOMAIN_UI, &ui_diag, sizeof(ui_diag));
    ESP_LOGI(TAG, "ui: frames_rendered=%u frames_dropped=%u glyph_fallback=%u",
             (unsigned)ui_diag.frames_rendered, (unsigned)ui_diag.frames_dropped,
             (unsigned)ui_diag.unrepresentable_glyph_count);

    resolve_task_handles_once();
    for (size_t i = 0; i < DIAG_TASK_COUNT; i++) {
        if (s_diag_task_handles[i] == NULL) {
            continue;   /* not resolved (created after this ran, or never created -- "ota") */
        }
        ESP_LOGI(TAG, "task %-16s stack_hwm=%u words",
                 DIAG_TASK_NAMES[i], (unsigned)uxTaskGetStackHighWaterMark(s_diag_task_handles[i]));
    }
}

size_t svc_diagnostics_get_fault_log(svc_diagnostics_t *d, fault_record_t *out, size_t out_capacity)
{
    return faultlog_load(&d->kv, out, out_capacity);
}
