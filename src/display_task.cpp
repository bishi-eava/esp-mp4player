#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "mp4_player.h"

static const char *TAG = "display";

// ============================================================
// display_task: ダブルバッファから RGB565 を SPI DMA で LCD に転送
// ============================================================
void display_task(void *arg)
{
    player_ctx_t *ctx = (player_ctx_t *)arg;
    LGFX *display = ctx->display;

    ESP_LOGI(TAG, "display_task started");
    display->fillScreen(TFT_BLACK);

    while (true) {
        // Wait for decode_task to fill a buffer
        if (xSemaphoreTake(ctx->decode_ready, pdMS_TO_TICKS(10000)) != pdTRUE) {
            if (ctx->pipeline_eos) break;
            continue;
        }

        if (ctx->pipeline_eos) {
            display->fillScreen(TFT_BLACK);
            display->setCursor(10, 10);
            display->setTextColor(TFT_GREEN, TFT_BLACK);
            display->println("Playback finished");
            break;
        }

        // Push the active buffer to LCD via SPI DMA
        int buf_idx = ctx->active_buf;
        display->pushImage(ctx->display_x, ctx->display_y,
                           ctx->scaled_w, ctx->scaled_h,
                           ctx->rgb_buf[buf_idx]);

        // Signal decode_task that this buffer is free
        xSemaphoreGive(ctx->display_done);
    }

    ESP_LOGI(TAG, "display_task done");
    vTaskDelete(nullptr);
}
