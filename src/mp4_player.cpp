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
#include "yuv2rgb.h"

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

static const char *TAG = "mp4player";

#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 240
#define READ_BUF_SIZE  (64 * 1024)

// --- minimp4 file read callback ---
static FILE *mp4_file = nullptr;

static int mp4_read_callback(int64_t offset, void *buffer, size_t size, void *token)
{
    (void)token;
    if (fseek(mp4_file, (long)offset, SEEK_SET) != 0) {
        return 1;
    }
    if (fread(buffer, 1, size, mp4_file) != size) {
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

// --- Play MP4 video ---
void play_mp4(LGFX &display, const char *filepath)
{
    ESP_LOGI(TAG, "Opening MP4 file: %s", filepath);

    mp4_file = fopen(filepath, "rb");
    if (!mp4_file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return;
    }

    fseek(mp4_file, 0, SEEK_END);
    int64_t file_size = ftell(mp4_file);
    fseek(mp4_file, 0, SEEK_SET);
    ESP_LOGI(TAG, "File size: %lld bytes", file_size);

    MP4D_demux_t mp4;
    if (!MP4D_open(&mp4, mp4_read_callback, nullptr, file_size)) {
        ESP_LOGE(TAG, "MP4D_open failed");
        fclose(mp4_file);
        return;
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
        fclose(mp4_file);
        return;
    }

    MP4D_track_t *tr = &mp4.track[video_track];
    unsigned timescale = tr->timescale;

    // Get actual video dimensions from MP4 metadata
    int video_w = tr->SampleDescription.video.width;
    int video_h = tr->SampleDescription.video.height;
    if (video_w <= 0 || video_w > DISPLAY_WIDTH) video_w = DISPLAY_WIDTH;
    if (video_h <= 0 || video_h > DISPLAY_HEIGHT) video_h = DISPLAY_HEIGHT;

    int display_x = (DISPLAY_WIDTH - video_w) / 2;
    int display_y = (DISPLAY_HEIGHT - video_h) / 2;

    ESP_LOGI(TAG, "Video: %dx%d, Display offset: (%d, %d)", video_w, video_h, display_x, display_y);

    // Allocate buffers based on actual video size
    uint16_t *rgb565_buf = (uint16_t *)heap_caps_malloc(video_w * video_h * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    uint8_t  *read_buf   = (uint8_t *)heap_caps_malloc(READ_BUF_SIZE, MALLOC_CAP_SPIRAM);
    uint8_t  *nal_buf    = (uint8_t *)heap_caps_malloc(READ_BUF_SIZE, MALLOC_CAP_SPIRAM);

    if (!rgb565_buf || !read_buf || !nal_buf) {
        ESP_LOGE(TAG, "Failed to allocate buffers in PSRAM");
        heap_caps_free(rgb565_buf);
        heap_caps_free(read_buf);
        heap_caps_free(nal_buf);
        MP4D_close(&mp4);
        fclose(mp4_file);
        return;
    }
    ESP_LOGI(TAG, "Buffers allocated: rgb565=%d bytes", video_w * video_h * (int)sizeof(uint16_t));

    display.fillScreen(TFT_BLACK);

    // Create H.264 decoder
    esp_h264_dec_cfg_t dec_cfg = {
        .pic_type = ESP_H264_RAW_FMT_I420,
    };
    esp_h264_dec_handle_t decoder = nullptr;

    esp_h264_err_t err = esp_h264_dec_sw_new(&dec_cfg, &decoder);
    if (err != ESP_H264_ERR_OK || !decoder) {
        ESP_LOGE(TAG, "Failed to create H.264 decoder: %d", err);
        heap_caps_free(rgb565_buf);
        heap_caps_free(read_buf);
        heap_caps_free(nal_buf);
        MP4D_close(&mp4);
        fclose(mp4_file);
        return;
    }

    err = esp_h264_dec_open(decoder);
    if (err != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open H.264 decoder: %d", err);
        esp_h264_dec_del(decoder);
        heap_caps_free(rgb565_buf);
        heap_caps_free(read_buf);
        heap_caps_free(nal_buf);
        MP4D_close(&mp4);
        fclose(mp4_file);
        return;
    }

    ESP_LOGI(TAG, "H.264 decoder initialized");

    // Feed SPS/PPS to decoder
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

        esp_h264_dec_in_frame_t in_frame = {};
        in_frame.raw_data.buffer = nal_buf;
        in_frame.raw_data.len = (uint32_t)nal_len;
        esp_h264_dec_out_frame_t out_frame = {};
        esp_h264_dec_process(decoder, &in_frame, &out_frame);
        ESP_LOGI(TAG, "SPS fed: %d bytes", sps_bytes);
    }

    if (pps && pps_bytes > 0) {
        int nal_len = 0;
        nal_buf[nal_len++] = 0x00;
        nal_buf[nal_len++] = 0x00;
        nal_buf[nal_len++] = 0x00;
        nal_buf[nal_len++] = 0x01;
        memcpy(nal_buf + nal_len, pps, pps_bytes);
        nal_len += pps_bytes;

        esp_h264_dec_in_frame_t in_frame = {};
        in_frame.raw_data.buffer = nal_buf;
        in_frame.raw_data.len = (uint32_t)nal_len;
        esp_h264_dec_out_frame_t out_frame = {};
        esp_h264_dec_process(decoder, &in_frame, &out_frame);
        ESP_LOGI(TAG, "PPS fed: %d bytes", pps_bytes);
    }

    // Decode and display loop
    unsigned total_frames = tr->sample_count;
    unsigned decoded_frames = 0;
    unsigned skipped_frames = 0;
    int64_t start_time = esp_timer_get_time();

    ESP_LOGI(TAG, "Starting playback: %d frames", total_frames);

    for (unsigned sample = 0; sample < total_frames; sample++) {
        unsigned frame_bytes = 0;
        unsigned timestamp = 0;
        unsigned duration = 0;

        MP4D_file_offset_t offset = MP4D_frame_offset(&mp4, video_track, sample,
                                                       &frame_bytes, &timestamp, &duration);

        if (frame_bytes == 0 || frame_bytes > READ_BUF_SIZE) {
            ESP_LOGW(TAG, "Frame %d: invalid size %d, skipping", sample, frame_bytes);
            skipped_frames++;
            continue;
        }

        if (fseek(mp4_file, (long)offset, SEEK_SET) != 0 ||
            fread(read_buf, 1, frame_bytes, mp4_file) != frame_bytes) {
            ESP_LOGE(TAG, "Failed to read frame %d", sample);
            break;
        }

        int nal_size = build_annex_b_nal(nal_buf, READ_BUF_SIZE, read_buf, frame_bytes);
        if (nal_size <= 0) {
            ESP_LOGW(TAG, "Frame %d: AVCC to Annex B conversion failed", sample);
            skipped_frames++;
            continue;
        }

        esp_h264_dec_in_frame_t in_frame = {};
        in_frame.raw_data.buffer = nal_buf;
        in_frame.raw_data.len = (uint32_t)nal_size;

        esp_h264_dec_out_frame_t out_frame = {};

        while (in_frame.raw_data.len > 0) {
            err = esp_h264_dec_process(decoder, &in_frame, &out_frame);
            if (err != ESP_H264_ERR_OK) {
                ESP_LOGW(TAG, "Decode error at frame %d: %d", sample, err);
                break;
            }

            if (in_frame.consume == 0) {
                ESP_LOGW(TAG, "Frame %d: decoder consumed 0 bytes, skipping", sample);
                break;
            }

            in_frame.raw_data.buffer += in_frame.consume;
            in_frame.raw_data.len -= in_frame.consume;

            if (out_frame.out_size > 0 && out_frame.outbuf) {
                i420_to_rgb565(out_frame.outbuf, rgb565_buf, video_w, video_h);
                display.pushImage(display_x, display_y, video_w, video_h, rgb565_buf);
                decoded_frames++;
            }
        }

        // Frame timing control
        if (timescale > 0 && duration > 0) {
            int64_t elapsed_us = esp_timer_get_time() - start_time;
            int64_t expected_us = (int64_t)timestamp * 1000000LL / timescale;
            int64_t delay_us = expected_us - elapsed_us;
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
    float avg_fps = decoded_frames / total_time_s;

    ESP_LOGI(TAG, "Playback complete: %d decoded, %d skipped, %.1f sec, %.1f fps",
             decoded_frames, skipped_frames, total_time_s, avg_fps);

    // Cleanup
    esp_h264_dec_close(decoder);
    esp_h264_dec_del(decoder);
    MP4D_close(&mp4);
    fclose(mp4_file);
    mp4_file = nullptr;

    heap_caps_free(rgb565_buf);
    heap_caps_free(read_buf);
    heap_caps_free(nal_buf);
}
