#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "mp4_player.h"
#include "board_config.h"

// Redirect minimp4 allocations to PSRAM (internal RAM is too limited for large track data)
static void* mp4_psram_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}
static void* mp4_psram_realloc(void *ptr, size_t size) {
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
}
#define malloc mp4_psram_malloc
#define realloc mp4_psram_realloc

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

#undef malloc
#undef realloc

static const char *TAG = "demux";

#define READ_BUF_SIZE  (64 * 1024)

// --- minimp4 file read callback ---
static int mp4_read_callback(int64_t offset, void *buffer, size_t size, void *token)
{
    FILE *f = (FILE *)token;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return 1;
    return (fread(buffer, 1, size, f) != size) ? 1 : 0;
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
    QueueHandle_t queue = ctx->nal_queue;

    ESP_LOGI(TAG, "demux_task started: %s", ctx->filepath);

    FILE *f = fopen(ctx->filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", ctx->filepath);
        goto send_eos;
    }

    // Increase stdio buffer to reduce SD card SPI transactions
    setvbuf(f, NULL, _IOFBF, 8 * 1024);

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

        // Set video dimensions in shared context (decode_task reads these)
        int vw = tr->SampleDescription.video.width;
        int vh = tr->SampleDescription.video.height;
        if (vw <= 0 || vh <= 0) {
            ESP_LOGE(TAG, "Invalid video dimensions: %dx%d", vw, vh);
            MP4D_close(&mp4);
            fclose(f);
            goto send_eos;
        }
        if (vw > BOARD_MAX_DECODE_WIDTH || vh > BOARD_MAX_DECODE_HEIGHT) {
            ESP_LOGE(TAG, "Video %dx%d exceeds max decode resolution %dx%d",
                     vw, vh, BOARD_MAX_DECODE_WIDTH, BOARD_MAX_DECODE_HEIGHT);
            MP4D_close(&mp4);
            fclose(f);
            goto send_eos;
        }
        ctx->video_w = vw;
        ctx->video_h = vh;
        ESP_LOGI(TAG, "Video dimensions: %dx%d", vw, vh);

#ifdef BOARD_HAS_AUDIO
        // Find AAC audio track
        int audio_track = -1;
        for (unsigned i = 0; i < mp4.track_count; i++) {
            if (mp4.track[i].handler_type == MP4D_HANDLER_TYPE_SOUN &&
                mp4.track[i].object_type_indication == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3) {
                audio_track = i;
                ESP_LOGI(TAG, "Found AAC audio track %d: %d ch, %d Hz, %d samples",
                         i,
                         mp4.track[i].SampleDescription.audio.channelcount,
                         mp4.track[i].SampleDescription.audio.samplerate_hz,
                         mp4.track[i].sample_count);
                break;
            }
        }

        if (audio_track >= 0) {
            MP4D_track_t *atr = &mp4.track[audio_track];
            ctx->audio_sample_rate = atr->SampleDescription.audio.samplerate_hz;
            ctx->audio_channels    = atr->SampleDescription.audio.channelcount;
            if (atr->dsi && atr->dsi_bytes > 0) {
                ctx->audio_dsi = (uint8_t *)heap_caps_malloc(atr->dsi_bytes, MALLOC_CAP_SPIRAM);
                if (ctx->audio_dsi) {
                    memcpy(ctx->audio_dsi, atr->dsi, atr->dsi_bytes);
                    ctx->audio_dsi_bytes = atr->dsi_bytes;
                }
            }
        } else {
            ESP_LOGW(TAG, "No AAC audio track found, video-only playback");
        }
#endif

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

        // Send SPS/PPS to decode_task
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
        ESP_LOGI(TAG, "Starting demux: %d video frames, timescale=%u", total_frames, timescale);

#ifdef BOARD_HAS_AUDIO
        if (audio_track >= 0 && ctx->audio_queue) {
            // Time-ordered interleaved demux (video + audio)
            MP4D_track_t *atr = &mp4.track[audio_track];
            unsigned audio_timescale = atr->timescale;
            unsigned total_audio_frames = atr->sample_count;
            ESP_LOGI(TAG, "Interleaved demux: %d audio frames, timescale=%u",
                     total_audio_frames, audio_timescale);

            unsigned v_sample = 0;
            unsigned a_sample = 0;

            while (v_sample < total_frames || a_sample < total_audio_frames) {
                int64_t v_pts = INT64_MAX;
                int64_t a_pts = INT64_MAX;
                unsigned v_bytes = 0, v_ts = 0, v_dur = 0;
                unsigned a_bytes = 0, a_ts = 0, a_dur = 0;
                MP4D_file_offset_t v_offset = 0, a_offset = 0;

                if (v_sample < total_frames) {
                    v_offset = MP4D_frame_offset(&mp4, video_track, v_sample,
                                                  &v_bytes, &v_ts, &v_dur);
                    v_pts = (timescale > 0) ? (int64_t)v_ts * 1000000LL / timescale : 0;
                }
                if (a_sample < total_audio_frames) {
                    a_offset = MP4D_frame_offset(&mp4, audio_track, a_sample,
                                                  &a_bytes, &a_ts, &a_dur);
                    a_pts = (audio_timescale > 0) ? (int64_t)a_ts * 1000000LL / audio_timescale : 0;
                }

                bool send_video = (v_sample < total_frames) &&
                                  (v_pts <= a_pts || a_sample >= total_audio_frames);

                if (send_video) {
                    // Send video frame
                    if (v_bytes == 0 || v_bytes > READ_BUF_SIZE) {
                        v_sample++;
                        continue;
                    }
                    if (fseek(f, (long)v_offset, SEEK_SET) != 0 ||
                        fread(read_buf, 1, v_bytes, f) != v_bytes) {
                        ESP_LOGE(TAG, "Failed to read video frame %d", v_sample);
                        break;
                    }
                    int nal_size = build_annex_b_nal(nal_buf, READ_BUF_SIZE, read_buf, v_bytes);
                    if (nal_size <= 0) {
                        v_sample++;
                        continue;
                    }
                    if (!send_nal_to_queue(queue, nal_buf, nal_size, v_pts, false)) {
                        ESP_LOGE(TAG, "Failed to send video frame %d", v_sample);
                        break;
                    }
                    v_sample++;
                } else {
                    // Send audio frame
                    if (a_bytes == 0 || a_bytes > READ_BUF_SIZE) {
                        a_sample++;
                        continue;
                    }
                    if (fseek(f, (long)a_offset, SEEK_SET) != 0 ||
                        fread(read_buf, 1, a_bytes, f) != a_bytes) {
                        ESP_LOGE(TAG, "Failed to read audio frame %d", a_sample);
                        break;
                    }

                    uint8_t *abuf = (uint8_t *)heap_caps_malloc(a_bytes, MALLOC_CAP_SPIRAM);
                    if (!abuf) {
                        ESP_LOGE(TAG, "Failed to alloc audio frame %d", a_sample);
                        break;
                    }
                    memcpy(abuf, read_buf, a_bytes);

                    audio_msg_t amsg = {};
                    amsg.data   = abuf;
                    amsg.size   = a_bytes;
                    amsg.pts_us = a_pts;
                    amsg.eos    = false;

                    if (xQueueSend(ctx->audio_queue, &amsg, pdMS_TO_TICKS(5000)) != pdTRUE) {
                        ESP_LOGE(TAG, "Audio queue send timeout");
                        heap_caps_free(abuf);
                        break;
                    }
                    a_sample++;
                }
            }
        } else
#endif
        {
            // Video-only frame loop (original path)
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
        }

        heap_caps_free(read_buf);
        heap_caps_free(nal_buf);
        MP4D_close(&mp4);
        fclose(f);
    }

send_eos:
#ifdef BOARD_HAS_AUDIO
    if (ctx->audio_queue) {
        audio_msg_t aeos = {};
        aeos.eos = true;
        xQueueSend(ctx->audio_queue, &aeos, portMAX_DELAY);
        ESP_LOGI(TAG, "Audio EOS sent");
    }
#endif
    {
        frame_msg_t eos = {};
        eos.eos = true;
        xQueueSend(queue, &eos, portMAX_DELAY);
        ESP_LOGI(TAG, "EOS sent, exiting");
    }
    vTaskDelete(nullptr);
}
