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
    vTaskDelete(nullptr);
}

void DisplayStage::run()
{
    ESP_LOGI(TAG, "display_task started");
    display_.fillScreen(TFT_BLACK);

    while (true) {
        if (xSemaphoreTake(sync_.decode_ready, pdMS_TO_TICKS(kSemaphoreTimeoutMs)) != pdTRUE) {
            if (sync_.pipeline_eos) break;
            continue;
        }

        if (sync_.pipeline_eos) {
            display_.fillScreen(TFT_BLACK);
            display_.setCursor(10, 10);
            display_.setTextColor(TFT_GREEN, TFT_BLACK);
            display_.println("Playback finished");
            break;
        }

        display_.pushImage(video_info_.display_x, video_info_.display_y,
                           video_info_.scaled_w, video_info_.scaled_h,
                           dbuf_.read_buf());

        xSemaphoreGive(sync_.display_done);
    }

    ESP_LOGI(TAG, "display_task done");
}

}  // namespace mp4
