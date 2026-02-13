#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "mp4_player.h"

static const char *TAG = "monitor";

#define MONITOR_INTERVAL_MS  3000
#define MAX_TASKS            16

void monitor_task(void *arg)
{
    ESP_LOGI(TAG, "monitor_task started (interval=%dms)", MONITOR_INTERVAL_MS);

    TaskStatus_t *task_status = (TaskStatus_t *)malloc(MAX_TASKS * sizeof(TaskStatus_t));
    if (!task_status) {
        ESP_LOGE(TAG, "Failed to allocate task status array");
        vTaskDelete(nullptr);
        return;
    }

    uint32_t prev_total = 0;
    uint32_t prev_runtime[MAX_TASKS] = {};
    char     prev_names[MAX_TASKS][configMAX_TASK_NAME_LEN] = {};
    UBaseType_t prev_count = 0;

    // Initial snapshot (first delta is meaningless, discard)
    {
        uint32_t total;
        UBaseType_t count = uxTaskGetSystemState(task_status, MAX_TASKS, &total);
        prev_total = total;
        prev_count = count;
        for (UBaseType_t i = 0; i < count && i < MAX_TASKS; i++) {
            prev_runtime[i] = task_status[i].ulRunTimeCounter;
            strncpy(prev_names[i], task_status[i].pcTaskName,
                    configMAX_TASK_NAME_LEN - 1);
        }
    }

    vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));

    while (true) {
        uint32_t total;
        UBaseType_t count = uxTaskGetSystemState(task_status, MAX_TASKS, &total);

        uint32_t delta_total = total - prev_total;
        if (delta_total == 0) {
            vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
            continue;
        }

        // Each core contributes half of total runtime
        uint32_t per_core_time = delta_total / 2;

        uint32_t core0_busy = 0, core1_busy = 0;

        ESP_LOGI(TAG, "=== CPU Load (last %.1fs) ===",
                 MONITOR_INTERVAL_MS / 1000.0f);

        for (UBaseType_t i = 0; i < count && i < MAX_TASKS; i++) {
            // Find previous runtime for this task by name
            uint32_t prev_rt = 0;
            for (UBaseType_t j = 0; j < prev_count && j < MAX_TASKS; j++) {
                if (strcmp(task_status[i].pcTaskName, prev_names[j]) == 0) {
                    prev_rt = prev_runtime[j];
                    break;
                }
            }

            uint32_t delta_task = task_status[i].ulRunTimeCounter - prev_rt;
            float pct = (per_core_time > 0)
                ? (float)delta_task * 100.0f / per_core_time
                : 0.0f;

            BaseType_t core_id = task_status[i].xCoreID;
            bool is_idle = (strncmp(task_status[i].pcTaskName, "IDLE", 4) == 0);

            if (!is_idle && pct >= 0.1f) {
                const char *core_str = (core_id == 0) ? "C0" :
                                       (core_id == 1) ? "C1" : "C*";
                ESP_LOGI(TAG, "  %-12s [%s] %5.1f%%",
                         task_status[i].pcTaskName, core_str, pct);
            }

            // Accumulate per-core busy time (exclude IDLE)
            if (!is_idle) {
                if (core_id == 0) {
                    core0_busy += delta_task;
                } else if (core_id == 1) {
                    core1_busy += delta_task;
                } else {
                    // Floating task: split evenly
                    core0_busy += delta_task / 2;
                    core1_busy += delta_task / 2;
                }
            }
        }

        float core0_pct = (per_core_time > 0)
            ? (float)core0_busy * 100.0f / per_core_time : 0.0f;
        float core1_pct = (per_core_time > 0)
            ? (float)core1_busy * 100.0f / per_core_time : 0.0f;

        ESP_LOGI(TAG, "  Core 0: %5.1f%%  Core 1: %5.1f%%",
                 core0_pct, core1_pct);
        ESP_LOGI(TAG, "  Heap: %u internal, %u PSRAM",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

        // Save current snapshot for next delta
        prev_total = total;
        prev_count = count;
        for (UBaseType_t i = 0; i < count && i < MAX_TASKS; i++) {
            prev_runtime[i] = task_status[i].ulRunTimeCounter;
            strncpy(prev_names[i], task_status[i].pcTaskName,
                    configMAX_TASK_NAME_LEN - 1);
        }

        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}
