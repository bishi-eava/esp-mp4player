#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "esp_h264_dec.h"

extern "C" esp_h264_err_t esp_h264_dec_sw_new(const esp_h264_dec_cfg_t *cfg, esp_h264_dec_handle_t *dec);

#include "mp4_player.h"
#include "board_config.h"
#include "yuv2rgb.h"

static const char *TAG = "decode";

// ============================================================
// decode_task: queue から NAL 受信 → H.264 decode → RGB565 変換
//              → ダブルバッファ経由で display_task に渡す
// ============================================================
void decode_task(void *arg)
{
    player_ctx_t *ctx = (player_ctx_t *)arg;
    QueueHandle_t queue = ctx->nal_queue;

    ESP_LOGI(TAG, "decode_task started");

    // Wait for demux_task to set video dimensions (first SPS/PPS signals readiness)
    frame_msg_t first_msg;
    if (xQueuePeek(queue, &first_msg, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed waiting for first frame from demux");
        goto signal_eos;
    }

    {
        int video_w = ctx->video_w;
        int video_h = ctx->video_h;
        if (video_w <= 0) video_w = BOARD_DISPLAY_WIDTH;
        if (video_h <= 0) video_h = BOARD_DISPLAY_HEIGHT;

        // Calculate aspect-ratio-preserving scale (integer math, downscale only)
        int scaled_w, scaled_h;
        bool needs_scaling = (video_w > BOARD_DISPLAY_WIDTH || video_h > BOARD_DISPLAY_HEIGHT);
        if (needs_scaling) {
            // Cross-multiply to compare ratios without float:
            // LCD_W/video_w vs LCD_H/video_h  →  LCD_W*video_h vs LCD_H*video_w
            if (BOARD_DISPLAY_WIDTH * video_h <= BOARD_DISPLAY_HEIGHT * video_w) {
                // Width-constrained
                scaled_w = BOARD_DISPLAY_WIDTH;
                scaled_h = video_h * BOARD_DISPLAY_WIDTH / video_w;
            } else {
                // Height-constrained
                scaled_h = BOARD_DISPLAY_HEIGHT;
                scaled_w = video_w * BOARD_DISPLAY_HEIGHT / video_h;
            }
            // Ensure even dimensions for YUV subsampling
            scaled_w &= ~1;
            scaled_h &= ~1;
        } else {
            scaled_w = video_w;
            scaled_h = video_h;
        }

        ctx->scaled_w = scaled_w;
        ctx->scaled_h = scaled_h;
        ctx->display_x = (BOARD_DISPLAY_WIDTH - scaled_w) / 2;
        ctx->display_y = (BOARD_DISPLAY_HEIGHT - scaled_h) / 2;

        ESP_LOGI(TAG, "Video: %dx%d -> scaled: %dx%d, offset: (%d,%d)",
                 video_w, video_h, scaled_w, scaled_h, ctx->display_x, ctx->display_y);

        // Allocate RGB565 double buffers in PSRAM (scaled size)
        size_t buf_size = scaled_w * scaled_h * sizeof(uint16_t);
        ctx->rgb_buf[0] = (uint16_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
        ctx->rgb_buf[1] = (uint16_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
        if (!ctx->rgb_buf[0] || !ctx->rgb_buf[1]) {
            ESP_LOGE(TAG, "Failed to allocate RGB565 double buffers (%d bytes each)", buf_size);
            heap_caps_free(ctx->rgb_buf[0]);
            heap_caps_free(ctx->rgb_buf[1]);
            goto signal_eos;
        }
        ESP_LOGI(TAG, "Double buffer allocated: 2 x %d bytes in PSRAM", buf_size);

        // Give initial display_done so decode can start writing to buf[0]
        xSemaphoreGive(ctx->display_done);

        // Create H.264 decoder
        esp_h264_dec_cfg_t dec_cfg = {
            .pic_type = ESP_H264_RAW_FMT_I420,
        };
        esp_h264_dec_handle_t decoder = nullptr;

        esp_h264_err_t err = esp_h264_dec_sw_new(&dec_cfg, &decoder);
        if (err != ESP_H264_ERR_OK || !decoder) {
            ESP_LOGE(TAG, "Failed to create H.264 decoder: %d", err);
            heap_caps_free(ctx->rgb_buf[0]);
            heap_caps_free(ctx->rgb_buf[1]);
            goto signal_eos;
        }

        err = esp_h264_dec_open(decoder);
        if (err != ESP_H264_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open H.264 decoder: %d", err);
            esp_h264_dec_del(decoder);
            heap_caps_free(ctx->rgb_buf[0]);
            heap_caps_free(ctx->rgb_buf[1]);
            goto signal_eos;
        }

        ESP_LOGI(TAG, "H.264 decoder initialized");

        unsigned decoded_frames = 0;
        unsigned skipped_frames = 0;
        int64_t start_time = esp_timer_get_time();
        int write_idx = 0;

        // Receive and decode loop
        frame_msg_t msg;
        while (true) {
            if (xQueueReceive(queue, &msg, pdMS_TO_TICKS(10000)) != pdTRUE) {
                ESP_LOGW(TAG, "Queue receive timeout");
                continue;
            }

            if (msg.eos) {
                ESP_LOGI(TAG, "EOS received");
                break;
            }

            esp_h264_dec_in_frame_t in_frame = {};
            in_frame.raw_data.buffer = msg.data;
            in_frame.raw_data.len = (uint32_t)msg.size;

            esp_h264_dec_out_frame_t out_frame = {};

            while (in_frame.raw_data.len > 0) {
                err = esp_h264_dec_process(decoder, &in_frame, &out_frame);
                if (err != ESP_H264_ERR_OK) {
                    if (!msg.is_sps_pps) {
                        ESP_LOGW(TAG, "Decode error: %d", err);
                        skipped_frames++;
                    }
                    break;
                }

                if (in_frame.consume == 0) break;

                in_frame.raw_data.buffer += in_frame.consume;
                in_frame.raw_data.len -= in_frame.consume;

                if (out_frame.out_size > 0 && out_frame.outbuf) {
                    // Wait for display_task to finish with previous buffer
                    xSemaphoreTake(ctx->display_done, portMAX_DELAY);

                    // Convert YUV→RGB565 into write buffer
                    if (needs_scaling) {
                        i420_to_rgb565_scaled(out_frame.outbuf, ctx->rgb_buf[write_idx],
                                              video_w, video_h, scaled_w, scaled_h);
                    } else {
                        i420_to_rgb565(out_frame.outbuf, ctx->rgb_buf[write_idx], video_w, video_h);
                    }

                    // Signal display_task with the buffer index
                    ctx->active_buf = write_idx;
                    xSemaphoreGive(ctx->decode_ready);

                    // Swap buffer for next frame
                    write_idx ^= 1;
                    decoded_frames++;
                }
            }

            heap_caps_free(msg.data);

            // Frame timing control based on PTS
            if (!msg.is_sps_pps && msg.pts_us > 0) {
                int64_t elapsed_us = esp_timer_get_time() - start_time;
                int64_t delay_us = msg.pts_us - elapsed_us;
                if (delay_us > 1000) {
                    vTaskDelay(pdMS_TO_TICKS(delay_us / 1000));
                } else {
                    vTaskDelay(1);
                }
            } else {
                vTaskDelay(1);
            }
        }

        // Wait for last display to finish
        xSemaphoreTake(ctx->display_done, pdMS_TO_TICKS(1000));

        int64_t total_time_us = esp_timer_get_time() - start_time;
        float total_time_s = total_time_us / 1000000.0f;
        float avg_fps = (total_time_s > 0) ? decoded_frames / total_time_s : 0;

        ESP_LOGI(TAG, "Playback complete: %d decoded, %d skipped, %.1f sec, %.1f fps",
                 decoded_frames, skipped_frames, total_time_s, avg_fps);

        esp_h264_dec_close(decoder);
        esp_h264_dec_del(decoder);
        heap_caps_free(ctx->rgb_buf[0]);
        heap_caps_free(ctx->rgb_buf[1]);
        ctx->rgb_buf[0] = nullptr;
        ctx->rgb_buf[1] = nullptr;
    }

signal_eos:
    // Signal display_task to exit
    ctx->pipeline_eos = true;
    xSemaphoreGive(ctx->decode_ready);

    // Drain remaining queue messages
    {
        frame_msg_t msg;
        while (xQueueReceive(queue, &msg, 0) == pdTRUE) {
            if (msg.data) heap_caps_free(msg.data);
            if (msg.eos) break;
        }
    }

    ESP_LOGI(TAG, "decode_task done");
    vTaskDelete(nullptr);
}
