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
#define DIAG_MAX_TASKS   16

static const char *TAG = "svc_diagnostics";

typedef struct {
    bool     valid;
    uint32_t exc_pc;
    char     exc_task[FAULTLOG_TASK_NAME_MAX];
} coredump_marker_t;

bool svc_diagnostics_create(svc_diagnostics_t *d, core_eventbus_t *bus, kv_store_if_t kv,
                              time_source_t clock)
{
    memset(d, 0, sizeof(*d));
    d->bus   = bus;
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

    static TaskStatus_t tasks[DIAG_MAX_TASKS];
    UBaseType_t n = uxTaskGetSystemState(tasks, DIAG_MAX_TASKS, NULL);
    for (UBaseType_t i = 0; i < n; i++) {
        ESP_LOGI(TAG, "task %-16s stack_hwm=%u words",
                 tasks[i].pcTaskName, (unsigned)tasks[i].usStackHighWaterMark);
    }
}

size_t svc_diagnostics_get_fault_log(svc_diagnostics_t *d, fault_record_t *out, size_t out_capacity)
{
    return faultlog_load(&d->kv, out, out_capacity);
}
