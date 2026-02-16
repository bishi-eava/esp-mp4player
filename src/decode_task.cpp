#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_h264_dec.h"

extern "C" esp_h264_err_t esp_h264_dec_sw_new(const esp_h264_dec_cfg_t *cfg, esp_h264_dec_handle_t *dec);

#include "mp4_player.h"
#include "board_config.h"
#include "yuv2rgb.h"

static const char *TAG = "decode";

namespace mp4 {

void DecodeStage::task_func(void *arg)
{
    auto *self = static_cast<DecodeStage *>(arg);
    self->run();
    vTaskDelete(nullptr);
}

void DecodeStage::compute_scaling(int video_w, int video_h)
{
    bool needs_scaling = (video_w > BOARD_DISPLAY_WIDTH || video_h > BOARD_DISPLAY_HEIGHT);
    if (needs_scaling) {
        if (BOARD_DISPLAY_WIDTH * video_h <= BOARD_DISPLAY_HEIGHT * video_w) {
            video_info_.scaled_w = BOARD_DISPLAY_WIDTH;
            video_info_.scaled_h = video_h * BOARD_DISPLAY_WIDTH / video_w;
        } else {
            video_info_.scaled_h = BOARD_DISPLAY_HEIGHT;
            video_info_.scaled_w = video_w * BOARD_DISPLAY_HEIGHT / video_h;
        }
        video_info_.scaled_w &= ~1;
        video_info_.scaled_h &= ~1;
    } else {
        video_info_.scaled_w = video_w;
        video_info_.scaled_h = video_h;
    }
    video_info_.display_x = (BOARD_DISPLAY_WIDTH - video_info_.scaled_w) / 2;
    video_info_.display_y = (BOARD_DISPLAY_HEIGHT - video_info_.scaled_h) / 2;
}

void DecodeStage::drain_queue()
{
    FrameMsg msg;
    while (xQueueReceive(sync_.nal_queue, &msg, 0) == pdTRUE) {
        safe_free(msg.data);
        if (msg.eos) break;
    }
}

void DecodeStage::run()
{
    ESP_LOGI(TAG, "decode_task started");

    // Wait for demux_task to set video dimensions
    FrameMsg first_msg;
    if (xQueuePeek(sync_.nal_queue, &first_msg, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed waiting for first frame from demux");
        goto signal_eos;
    }

    {
        int video_w = video_info_.video_w;
        int video_h = video_info_.video_h;
        if (video_w <= 0) video_w = BOARD_DISPLAY_WIDTH;
        if (video_h <= 0) video_h = BOARD_DISPLAY_HEIGHT;

        compute_scaling(video_w, video_h);

        int scaled_w = video_info_.scaled_w;
        int scaled_h = video_info_.scaled_h;
        bool needs_scaling = (video_w > BOARD_DISPLAY_WIDTH || video_h > BOARD_DISPLAY_HEIGHT);

        ESP_LOGI(TAG, "Video: %dx%d -> scaled: %dx%d, offset: (%d,%d)",
                 video_w, video_h, scaled_w, scaled_h,
                 video_info_.display_x, video_info_.display_y);

        if (!dbuf_.init(scaled_w, scaled_h)) {
            size_t buf_size = scaled_w * scaled_h * sizeof(uint16_t);
            ESP_LOGE(TAG, "Failed to allocate RGB565 double buffers (%d bytes each)", buf_size);
            dbuf_.deinit();
            goto signal_eos;
        }
        ESP_LOGI(TAG, "Double buffer allocated: 2 x %d bytes in PSRAM",
                 (int)(scaled_w * scaled_h * sizeof(uint16_t)));

        xSemaphoreGive(sync_.display_done);

        // Create H.264 decoder
        esp_h264_dec_cfg_t dec_cfg = {
            .pic_type = ESP_H264_RAW_FMT_I420,
        };
        esp_h264_dec_handle_t decoder = nullptr;

        esp_h264_err_t err = esp_h264_dec_sw_new(&dec_cfg, &decoder);
        if (err != ESP_H264_ERR_OK || !decoder) {
            ESP_LOGE(TAG, "Failed to create H.264 decoder: %d", err);
            dbuf_.deinit();
            goto signal_eos;
        }

        err = esp_h264_dec_open(decoder);
        if (err != ESP_H264_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open H.264 decoder: %d", err);
            esp_h264_dec_del(decoder);
            dbuf_.deinit();
            goto signal_eos;
        }

        ESP_LOGI(TAG, "H.264 decoder initialized");

        unsigned decoded_frames = 0;
        unsigned skipped_frames = 0;
        int64_t start_time = esp_timer_get_time();

        FrameMsg msg;
        while (true) {
            if (xQueueReceive(sync_.nal_queue, &msg, pdMS_TO_TICKS(kQueueRecvTimeoutMs)) != pdTRUE) {
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
                    xSemaphoreTake(sync_.display_done, portMAX_DELAY);

                    if (needs_scaling) {
                        i420_to_rgb565_scaled(out_frame.outbuf, dbuf_.write_buf(),
                                               video_w, video_h, scaled_w, scaled_h);
                    } else {
                        i420_to_rgb565(out_frame.outbuf, dbuf_.write_buf(), video_w, video_h);
                    }

                    dbuf_.swap();
                    xSemaphoreGive(sync_.decode_ready);

                    decoded_frames++;
                }
            }

            psram_free(msg.data);

            // PTS timing
            if (!msg.is_sps_pps && msg.pts_us > 0) {
                int64_t elapsed_us = esp_timer_get_time() - start_time;
                int64_t delay_us = msg.pts_us - elapsed_us;
                if (delay_us > 1000) {
                    vTaskDelay(pdMS_TO_TICKS(delay_us / 1000));
                } else if (!sync_.audio_priority) {
                    vTaskDelay(1);  // full_video: yield CPU when behind
                }
            }
        }

        xSemaphoreTake(sync_.display_done, pdMS_TO_TICKS(kFinalDisplayWaitMs));

        int64_t total_time_us = esp_timer_get_time() - start_time;
        float total_time_s = total_time_us / 1000000.0f;
        float avg_fps = (total_time_s > 0) ? decoded_frames / total_time_s : 0;

        ESP_LOGI(TAG, "Playback complete: %d decoded, %d skipped, %.1f sec, %.1f fps",
                 decoded_frames, skipped_frames, total_time_s, avg_fps);

        esp_h264_dec_close(decoder);
        esp_h264_dec_del(decoder);
        dbuf_.deinit();
    }

signal_eos:
    sync_.pipeline_eos = true;
    xSemaphoreGive(sync_.decode_ready);
    drain_queue();

    ESP_LOGI(TAG, "decode_task done");
}

}  // namespace mp4
