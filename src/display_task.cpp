#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "mp4_player.h"

static const char *TAG = "display";

namespace mp4 {

void DisplayStage::task_func(void *arg)
{
    auto *self = static_cast<DisplayStage *>(arg);
    self->run();
    xEventGroupSetBits(self->sync_.task_done, PipelineSync::kDisplayDone);
    delete self;
    vTaskDelete(nullptr);
}

void DisplayStage::run()
{
    ESP_LOGI(TAG, "display_task started");
    display_.fillScreen(TFT_BLACK);

    while (true) {
        if (xSemaphoreTake(sync_.decode_ready, pdMS_TO_TICKS(500)) != pdTRUE) {
            if (sync_.pipeline_eos || sync_.stop_requested) break;
            continue;
        }

        if (sync_.pipeline_eos) break;

        display_.pushImage(video_info_.display_x, video_info_.display_y,
                           video_info_.scaled_w, video_info_.scaled_h,
                           dbuf_.read_buf());

        xSemaphoreGive(sync_.display_done);
    }

    // Unblock decode stage if it's waiting for display_done
    xSemaphoreGive(sync_.display_done);
    display_.fillScreen(TFT_BLACK);

    ESP_LOGI(TAG, "display_task done");
}

}  // namespace mp4
