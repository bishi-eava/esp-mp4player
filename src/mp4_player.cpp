#include <cstdio>
#include <cstring>
#include <cstdlib>

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

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

static const char *TAG = "mp4player";

#define READ_BUF_SIZE  (64 * 1024)

// --- minimp4 file read callback ---
// demux_task 内のローカル変数を参照するため、token に FILE* を渡す
static int mp4_read_callback(int64_t offset, void *buffer, size_t size, void *token)
{
    FILE *f = (FILE *)token;
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        return 1;
    }
    if (fread(buffer, 1, size, f) != size) {
        return 1;
    }
    return 0;
}

// --- Build Annex B NAL unit from AVCC data ---
static int build_annex_b_nal(uint8_t *dst, int dst_capacity,
                              const uint8_t *src, int src_size)
{
    int dst_pos = 0;
    int src_pos = 0;

    while (src_pos < src_size) {
        if (src_pos + 4 > src_size) break;

        uint32_t nal_size = ((uint32_t)src[src_pos] << 24) |
                            ((uint32_t)src[src_pos + 1] << 16) |
                            ((uint32_t)src[src_pos + 2] << 8) |
                            ((uint32_t)src[src_pos + 3]);
        src_pos += 4;

        if ((int)(src_pos + nal_size) > src_size) break;
        if (dst_pos + 4 + (int)nal_size > dst_capacity) break;

        dst[dst_pos++] = 0x00;
        dst[dst_pos++] = 0x00;
        dst[dst_pos++] = 0x00;
        dst[dst_pos++] = 0x01;

        memcpy(dst + dst_pos, src + src_pos, nal_size);
        dst_pos += nal_size;
        src_pos += nal_size;
    }
    return dst_pos;
}

// --- Send NAL data via queue (allocates PSRAM copy) ---
static bool send_nal_to_queue(QueueHandle_t queue, const uint8_t *nal_data, int nal_size,
                               int64_t pts_us, bool is_sps_pps)
{
    uint8_t *buf = (uint8_t *)heap_caps_malloc(nal_size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes for queue frame", nal_size);
        return false;
    }
    memcpy(buf, nal_data, nal_size);

    frame_msg_t msg = {};
    msg.data = buf;
    msg.size = nal_size;
    msg.pts_us = pts_us;
    msg.is_sps_pps = is_sps_pps;
    msg.eos = false;

    if (xQueueSend(queue, &msg, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Queue send timeout");
        heap_caps_free(buf);
        return false;
    }
    return true;
}

// ============================================================
// demux_task: MP4 demux + SD I/O → queue へ NAL フレーム送信
// ============================================================
void demux_task(void *arg)
{
    player_ctx_t *ctx = (player_ctx_t *)arg;
    QueueHandle_t queue = ctx->video_queue;

    ESP_LOGI(TAG, "demux_task started: %s", ctx->filepath);

    FILE *f = fopen(ctx->filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", ctx->filepath);
        goto send_eos;
    }

    {
        fseek(f, 0, SEEK_END);
        int64_t file_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        ESP_LOGI(TAG, "File size: %lld bytes", file_size);

        MP4D_demux_t mp4;
        if (!MP4D_open(&mp4, mp4_read_callback, f, file_size)) {
            ESP_LOGE(TAG, "MP4D_open failed");
            fclose(f);
            goto send_eos;
        }

        ESP_LOGI(TAG, "MP4 tracks: %d", mp4.track_count);

        // Find H.264 video track
        int video_track = -1;
        for (unsigned i = 0; i < mp4.track_count; i++) {
            if (mp4.track[i].handler_type == MP4D_HANDLER_TYPE_VIDE &&
                mp4.track[i].object_type_indication == MP4_OBJECT_TYPE_AVC) {
                video_track = i;
                ESP_LOGI(TAG, "Found H.264 video track %d: %dx%d, %d samples",
                         i,
                         mp4.track[i].SampleDescription.video.width,
                         mp4.track[i].SampleDescription.video.height,
                         mp4.track[i].sample_count);
                break;
            }
        }

        if (video_track < 0) {
            ESP_LOGE(TAG, "No H.264 video track found");
            MP4D_close(&mp4);
            fclose(f);
            goto send_eos;
        }

        MP4D_track_t *tr = &mp4.track[video_track];
        unsigned timescale = tr->timescale;

        // Set video dimensions in shared context (video_task reads these)
        int vw = tr->SampleDescription.video.width;
        int vh = tr->SampleDescription.video.height;
        if (vw <= 0 || vw > BOARD_DISPLAY_WIDTH) vw = BOARD_DISPLAY_WIDTH;
        if (vh <= 0 || vh > BOARD_DISPLAY_HEIGHT) vh = BOARD_DISPLAY_HEIGHT;
        ctx->video_w = vw;
        ctx->video_h = vh;
        ESP_LOGI(TAG, "Video dimensions: %dx%d", vw, vh);

        // Allocate read/nal buffers
        uint8_t *read_buf = (uint8_t *)heap_caps_malloc(READ_BUF_SIZE, MALLOC_CAP_SPIRAM);
        uint8_t *nal_buf  = (uint8_t *)heap_caps_malloc(READ_BUF_SIZE, MALLOC_CAP_SPIRAM);

        if (!read_buf || !nal_buf) {
            ESP_LOGE(TAG, "Failed to allocate demux buffers in PSRAM");
            heap_caps_free(read_buf);
            heap_caps_free(nal_buf);
            MP4D_close(&mp4);
            fclose(f);
            goto send_eos;
        }

        // Send SPS/PPS to video_task
        int sps_bytes = 0, pps_bytes = 0;
        const void *sps = MP4D_read_sps(&mp4, video_track, 0, &sps_bytes);
        const void *pps = MP4D_read_pps(&mp4, video_track, 0, &pps_bytes);

        if (sps && sps_bytes > 0) {
            int nal_len = 0;
            nal_buf[nal_len++] = 0x00;
            nal_buf[nal_len++] = 0x00;
            nal_buf[nal_len++] = 0x00;
            nal_buf[nal_len++] = 0x01;
            memcpy(nal_buf + nal_len, sps, sps_bytes);
            nal_len += sps_bytes;
            send_nal_to_queue(queue, nal_buf, nal_len, 0, true);
            ESP_LOGI(TAG, "SPS sent: %d bytes", sps_bytes);
        }

        if (pps && pps_bytes > 0) {
            int nal_len = 0;
            nal_buf[nal_len++] = 0x00;
            nal_buf[nal_len++] = 0x00;
            nal_buf[nal_len++] = 0x00;
            nal_buf[nal_len++] = 0x01;
            memcpy(nal_buf + nal_len, pps, pps_bytes);
            nal_len += pps_bytes;
            send_nal_to_queue(queue, nal_buf, nal_len, 0, true);
            ESP_LOGI(TAG, "PPS sent: %d bytes", pps_bytes);
        }

        // Frame loop: read from SD → AVCC→AnnexB → send via queue
        unsigned total_frames = tr->sample_count;
        ESP_LOGI(TAG, "Starting demux: %d frames, timescale=%u", total_frames, timescale);

        for (unsigned sample = 0; sample < total_frames; sample++) {
            unsigned frame_bytes = 0;
            unsigned timestamp = 0;
            unsigned duration = 0;

            MP4D_file_offset_t offset = MP4D_frame_offset(&mp4, video_track, sample,
                                                           &frame_bytes, &timestamp, &duration);

            if (frame_bytes == 0 || frame_bytes > READ_BUF_SIZE) {
                ESP_LOGW(TAG, "Frame %d: invalid size %d, skipping", sample, frame_bytes);
                continue;
            }

            if (fseek(f, (long)offset, SEEK_SET) != 0 ||
                fread(read_buf, 1, frame_bytes, f) != frame_bytes) {
                ESP_LOGE(TAG, "Failed to read frame %d", sample);
                break;
            }

            int nal_size = build_annex_b_nal(nal_buf, READ_BUF_SIZE, read_buf, frame_bytes);
            if (nal_size <= 0) {
                ESP_LOGW(TAG, "Frame %d: AVCC to Annex B conversion failed", sample);
                continue;
            }

            int64_t pts_us = (timescale > 0) ? (int64_t)timestamp * 1000000LL / timescale : 0;

            if (!send_nal_to_queue(queue, nal_buf, nal_size, pts_us, false)) {
                ESP_LOGE(TAG, "Failed to send frame %d", sample);
                break;
            }
        }

        heap_caps_free(read_buf);
        heap_caps_free(nal_buf);
        MP4D_close(&mp4);
        fclose(f);
    }

send_eos:
    {
        frame_msg_t eos = {};
        eos.eos = true;
        xQueueSend(queue, &eos, portMAX_DELAY);
        ESP_LOGI(TAG, "demux_task: EOS sent, exiting");
    }
    vTaskDelete(nullptr);
}

// ============================================================
// video_task: queue から NAL 受信 → H.264 decode → display
// ============================================================
void video_task(void *arg)
{
    player_ctx_t *ctx = (player_ctx_t *)arg;
    LGFX *display = ctx->display;
    QueueHandle_t queue = ctx->video_queue;

    ESP_LOGI(TAG, "video_task started");

    // Wait for demux_task to set video dimensions (first SPS/PPS message signals readiness)
    // MP4 parsing can take 20+ seconds for large files, so wait indefinitely
    frame_msg_t first_msg;
    if (xQueuePeek(queue, &first_msg, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed waiting for first frame from demux");
        goto wait_eos;
    }

    {
        int video_w = ctx->video_w;
        int video_h = ctx->video_h;
        if (video_w <= 0) video_w = BOARD_DISPLAY_WIDTH;
        if (video_h <= 0) video_h = BOARD_DISPLAY_HEIGHT;
        int display_x = (BOARD_DISPLAY_WIDTH - video_w) / 2;
        int display_y = (BOARD_DISPLAY_HEIGHT - video_h) / 2;

        ESP_LOGI(TAG, "Video: %dx%d, display offset: (%d,%d)", video_w, video_h, display_x, display_y);

        // Allocate RGB565 buffer
        uint16_t *rgb565_buf = (uint16_t *)heap_caps_malloc(
            video_w * video_h * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!rgb565_buf) {
            ESP_LOGE(TAG, "Failed to allocate RGB565 buffer");
            goto wait_eos;
        }

        // Create H.264 decoder
        esp_h264_dec_cfg_t dec_cfg = {
            .pic_type = ESP_H264_RAW_FMT_I420,
        };
        esp_h264_dec_handle_t decoder = nullptr;

        esp_h264_err_t err = esp_h264_dec_sw_new(&dec_cfg, &decoder);
        if (err != ESP_H264_ERR_OK || !decoder) {
            ESP_LOGE(TAG, "Failed to create H.264 decoder: %d", err);
            heap_caps_free(rgb565_buf);
            goto wait_eos;
        }

        err = esp_h264_dec_open(decoder);
        if (err != ESP_H264_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open H.264 decoder: %d", err);
            esp_h264_dec_del(decoder);
            heap_caps_free(rgb565_buf);
            goto wait_eos;
        }

        ESP_LOGI(TAG, "H.264 decoder initialized");
        display->fillScreen(TFT_BLACK);

        unsigned decoded_frames = 0;
        unsigned skipped_frames = 0;
        int64_t start_time = esp_timer_get_time();

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
                    i420_to_rgb565(out_frame.outbuf, rgb565_buf, video_w, video_h);
                    display->pushImage(display_x, display_y, video_w, video_h, rgb565_buf);
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

        int64_t total_time_us = esp_timer_get_time() - start_time;
        float total_time_s = total_time_us / 1000000.0f;
        float avg_fps = (total_time_s > 0) ? decoded_frames / total_time_s : 0;

        ESP_LOGI(TAG, "Playback complete: %d decoded, %d skipped, %.1f sec, %.1f fps",
                 decoded_frames, skipped_frames, total_time_s, avg_fps);

        esp_h264_dec_close(decoder);
        esp_h264_dec_del(decoder);
        heap_caps_free(rgb565_buf);
    }

    // Show completion message
    display->fillScreen(TFT_BLACK);
    display->setCursor(10, 10);
    display->setTextColor(TFT_GREEN, TFT_BLACK);
    display->println("Playback finished");
    ESP_LOGI(TAG, "video_task done");
    vTaskDelete(nullptr);
    return;

wait_eos:
    // Drain queue until EOS (cleanup path)
    {
        frame_msg_t msg;
        while (xQueueReceive(queue, &msg, pdMS_TO_TICKS(10000)) == pdTRUE) {
            if (msg.data) heap_caps_free(msg.data);
            if (msg.eos) break;
        }
    }
    display->fillScreen(TFT_BLACK);
    display->setCursor(10, 10);
    display->setTextColor(TFT_RED, TFT_BLACK);
    display->println("Decoder init failed");
    ESP_LOGE(TAG, "video_task failed");
    vTaskDelete(nullptr);
}
