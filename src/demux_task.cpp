#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "mp4_player.h"
#include "board_config.h"

// Redirect minimp4 allocations to PSRAM (internal RAM is too limited for large track data)
#define malloc  mp4::psram_malloc
#define realloc mp4::psram_realloc

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

#undef malloc
#undef realloc

static const char *TAG = "demux";

// Check if sample is a sync sample (keyframe/IDR).
// sample_index is 0-based; stss entries are 1-based and sorted ascending.
static bool is_sync_sample(const MP4D_track_t *tr, unsigned sample_index)
{
    if (tr->sync_count == 0) return true;  // no stss box = all frames are sync
    unsigned one_based = sample_index + 1;
    for (unsigned i = 0; i < tr->sync_count; i++) {
        if (tr->sync_samples[i] == one_based) return true;
        if (tr->sync_samples[i] > one_based) break;
    }
    return false;
}

namespace mp4 {

void DemuxStage::task_func(void *arg)
{
    auto *self = static_cast<DemuxStage *>(arg);
    self->run();
    xEventGroupSetBits(self->sync_.task_done, PipelineSync::kDemuxDone);
    delete self;
    vTaskDelete(nullptr);
}

int DemuxStage::mp4_read_cb(int64_t offset, void *buffer, size_t size, void *token)
{
    FILE *f = (FILE *)token;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return 1;
    return (fread(buffer, 1, size, f) != size) ? 1 : 0;
}

int DemuxStage::build_annex_b_nal(uint8_t *dst, int capacity,
                                   const uint8_t *src, int size)
{
    int dst_pos = 0;
    int src_pos = 0;

    while (src_pos < size) {
        if (src_pos + 4 > size) break;

        uint32_t nal_size = ((uint32_t)src[src_pos] << 24) |
                            ((uint32_t)src[src_pos + 1] << 16) |
                            ((uint32_t)src[src_pos + 2] << 8) |
                            ((uint32_t)src[src_pos + 3]);
        src_pos += 4;

        if ((int)(src_pos + nal_size) > size) break;
        if (dst_pos + 4 + (int)nal_size > capacity) break;

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

bool DemuxStage::send_nal(const uint8_t *data, int size, int64_t pts_us, bool is_sps_pps)
{
    uint8_t *buf = psram_alloc<uint8_t>(size);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes for queue frame", size);
        return false;
    }
    memcpy(buf, data, size);

    FrameMsg msg = {};
    msg.data = buf;
    msg.size = size;
    msg.pts_us = pts_us;
    msg.is_sps_pps = is_sps_pps;
    msg.eos = false;

    if (xQueueSend(sync_.nal_queue, &msg, pdMS_TO_TICKS(kQueueSendTimeoutMs)) != pdTRUE) {
        ESP_LOGE(TAG, "Queue send timeout");
        psram_free(buf);
        return false;
    }
    return true;
}

bool DemuxStage::send_video_frame(const uint8_t *data, int size, int64_t pts_us)
{
    uint8_t *buf = psram_alloc<uint8_t>(size);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes for video frame", size);
        return false;
    }
    memcpy(buf, data, size);

    FrameMsg msg = {};
    msg.data = buf;
    msg.size = size;
    msg.pts_us = pts_us;
    msg.is_sps_pps = false;
    msg.eos = false;

    if (xQueueSend(sync_.nal_queue, &msg, pdMS_TO_TICKS(kVideoSendTimeoutMs)) != pdTRUE) {
        psram_free(buf);
        return false;
    }
    return true;
}

#ifdef BOARD_HAS_AUDIO
bool DemuxStage::send_audio(const uint8_t *data, int size, int64_t pts_us)
{
    uint8_t *buf = psram_alloc<uint8_t>(size);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to alloc audio frame %d bytes", size);
        return false;
    }
    memcpy(buf, data, size);

    AudioMsg msg = {};
    msg.data   = buf;
    msg.size   = size;
    msg.pts_us = pts_us;
    msg.eos    = false;

    if (xQueueSend(sync_.audio_queue, &msg, pdMS_TO_TICKS(kAudioSendTimeoutMs)) != pdTRUE) {
        ESP_LOGW(TAG, "Audio queue send timeout, skipping frame");
        psram_free(buf);
        return false;
    }
    return true;
}
#endif

void DemuxStage::send_eos()
{
    // Use short timeout — if queues are full during stop, downstream
    // stages will detect stop_requested and exit on their own.
    const TickType_t eos_timeout = pdMS_TO_TICKS(200);
#ifdef BOARD_HAS_AUDIO
    if (sync_.audio_queue) {
        AudioMsg aeos = {};
        aeos.eos = true;
        if (xQueueSend(sync_.audio_queue, &aeos, eos_timeout) == pdTRUE) {
            ESP_LOGI(TAG, "Audio EOS sent");
        } else {
            ESP_LOGW(TAG, "Audio EOS send timed out (stop in progress)");
        }
    }
#endif
    FrameMsg eos = {};
    eos.eos = true;
    if (xQueueSend(sync_.nal_queue, &eos, eos_timeout) == pdTRUE) {
        ESP_LOGI(TAG, "Video EOS sent");
    } else {
        ESP_LOGW(TAG, "Video EOS send timed out (stop in progress)");
    }
}

void DemuxStage::run()
{
    ESP_LOGI(TAG, "demux_task started: %s", filepath_);

    FILE *f = fopen(filepath_, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath_);
        send_eos();
        return;
    }

    // Allocate stdio buffer from internal RAM (faster CPU access than PSRAM,
    // avoids PSRAM bus contention with H.264 decoder on Core 1).
    // With USE_MALLOC + ALWAYSINTERNAL=4096, setvbuf(NULL) would land in PSRAM.
    char *f_stdio = static_cast<char*>(
        heap_caps_malloc(kStdioBufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (f_stdio) {
        setvbuf(f, f_stdio, _IOFBF, kStdioBufSize);
    } else {
        setvbuf(f, NULL, _IOFBF, kStdioBufSize);
    }

    {
        fseek(f, 0, SEEK_END);
        int64_t file_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        ESP_LOGI(TAG, "File size: %lld bytes", file_size);

        MP4D_demux_t mp4;
        if (!MP4D_open(&mp4, mp4_read_cb, f, file_size)) {
            ESP_LOGE(TAG, "MP4D_open failed");
            fclose(f);
            heap_caps_free(f_stdio);
            send_eos();
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
            fclose(f);
            heap_caps_free(f_stdio);
            send_eos();
            return;
        }

        MP4D_track_t *tr = &mp4.track[video_track];
        unsigned timescale = tr->timescale;

        int vw = tr->SampleDescription.video.width;
        int vh = tr->SampleDescription.video.height;
        if (vw <= 0 || vh <= 0) {
            ESP_LOGE(TAG, "Invalid video dimensions: %dx%d", vw, vh);
            MP4D_close(&mp4);
            fclose(f);
            heap_caps_free(f_stdio);
            send_eos();
            return;
        }
        if (vw > BOARD_MAX_DECODE_WIDTH || vh > BOARD_MAX_DECODE_HEIGHT) {
            ESP_LOGE(TAG, "Video %dx%d exceeds max decode resolution %dx%d",
                     vw, vh, BOARD_MAX_DECODE_WIDTH, BOARD_MAX_DECODE_HEIGHT);
            MP4D_close(&mp4);
            fclose(f);
            heap_caps_free(f_stdio);
            send_eos();
            return;
        }
        video_info_.video_w = vw;
        video_info_.video_h = vh;
        ESP_LOGI(TAG, "Video dimensions: %dx%d", vw, vh);

#ifdef BOARD_HAS_AUDIO
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
            audio_info_.sample_rate = atr->SampleDescription.audio.samplerate_hz;
            audio_info_.channels    = atr->SampleDescription.audio.channelcount;
            if (atr->dsi && atr->dsi_bytes > 0) {
                audio_info_.dsi = psram_alloc<uint8_t>(atr->dsi_bytes);
                if (audio_info_.dsi) {
                    memcpy(audio_info_.dsi, atr->dsi, atr->dsi_bytes);
                    audio_info_.dsi_bytes = atr->dsi_bytes;
                }
            }
        } else {
            ESP_LOGW(TAG, "No AAC audio track found, video-only playback");
        }
#endif

        // Allocate read/nal buffers
        uint8_t *read_buf = psram_alloc<uint8_t>(kReadBufSize);
        uint8_t *nal_buf  = psram_alloc<uint8_t>(kReadBufSize);

        if (!read_buf || !nal_buf) {
            ESP_LOGE(TAG, "Failed to allocate demux buffers in PSRAM");
            safe_free(read_buf);
            safe_free(nal_buf);
            MP4D_close(&mp4);
            fclose(f);
            heap_caps_free(f_stdio);
            send_eos();
            return;
        }

        // Send SPS/PPS
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
            send_nal(nal_buf, nal_len, 0, true);
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
            send_nal(nal_buf, nal_len, 0, true);
            ESP_LOGI(TAG, "PPS sent: %d bytes", pps_bytes);
        }

        // Done with FILE* — close it and switch to POSIX fd for frame reads.
        // MP4D_close only frees memory and doesn't use the read callback.
        // POSIX read() bypasses newlib's stdio layer which doesn't properly
        // buffer VFS-backed files (fread triggers a full 8KB physical SD read
        // on every call regardless of stdio buffer state).
        fclose(f);
        f = nullptr;
        heap_caps_free(f_stdio);
        f_stdio = nullptr;

        int v_fd = open(filepath_, O_RDONLY);
        if (v_fd < 0) {
            ESP_LOGE(TAG, "Failed to open video fd");
            psram_free(read_buf);
            psram_free(nal_buf);
            MP4D_close(&mp4);
            send_eos();
            return;
        }

        unsigned total_frames = tr->sample_count;
        const bool audio_prio = sync_.audio_priority;
        int64_t demux_wall_start = esp_timer_get_time();
        ESP_LOGI(TAG, "Starting demux: %d video frames, timescale=%u, sync_samples=%u, mode=%s",
                 total_frames, timescale, tr->sync_count,
                 audio_prio ? "audio_priority" : "full_video");
        if (tr->sync_count > 0 && timescale > 0) {
            for (unsigned i = 0; i < tr->sync_count; i++) {
                unsigned sample_idx = tr->sync_samples[i] - 1;  // 1-based to 0-based
                unsigned ts = 0, dur = 0, bytes = 0;
                MP4D_frame_offset(&mp4, video_track, sample_idx, &bytes, &ts, &dur);
                float pts_sec = (float)ts / timescale;
                ESP_LOGI(TAG, "  Keyframe[%u]: sample=%u, pts=%.2fs", i, tr->sync_samples[i], pts_sec);
            }
        }

#ifdef BOARD_HAS_AUDIO
        if (audio_track >= 0 && sync_.audio_queue) {
            // Separate POSIX fd for audio reads — each fd maintains its own
            // file position, so video/audio reads don't interfere.
            int a_fd = open(filepath_, O_RDONLY);
            bool separate_a_fd = (a_fd >= 0);
            if (!separate_a_fd) {
                ESP_LOGE(TAG, "Failed to open audio fd, sharing video fd");
                a_fd = v_fd;  // fallback: share video fd (needs lseek every time)
            }

            MP4D_track_t *atr = &mp4.track[audio_track];
            unsigned audio_timescale = atr->timescale;
            unsigned total_audio_frames = atr->sample_count;
            ESP_LOGI(TAG, "Interleaved demux: %d audio frames, timescale=%u",
                     total_audio_frames, audio_timescale);

            unsigned v_sample = 0;
            unsigned a_sample = 0;
            unsigned v_skipped = 0;
            int64_t demux_start_time = audio_prio ? esp_timer_get_time() : 0;

            // File position tracking — skip fseek when already at the right offset.
            // newlib fseek invalidates the stdio read buffer even for adjacent positions,
            // forcing a full physical SD read on the next fread.  By tracking position
            // and skipping unnecessary fseeks, sequential reads within MP4 chunks
            // benefit from the 8KB stdio buffer (audio: ~21 frames per buffer fill).
            int64_t f_pos = -1;   // video FILE position (-1 = unknown)
            int64_t af_pos = -1;  // audio FILE position (-1 = unknown)

            // Timing instrumentation
            int64_t total_v_read_us = 0, total_a_read_us = 0;
            int64_t total_v_send_us = 0, total_a_send_us = 0;
            uint32_t v_sent = 0, a_sent = 0, a_dropped = 0;
            uint32_t v_seeks = 0, v_seek_skips = 0;
            uint32_t a_seeks = 0, a_seek_skips = 0;

            while (v_sample < total_frames || a_sample < total_audio_frames) {
                if (sync_.stop_requested) {
                    ESP_LOGI(TAG, "Stop requested, ending demux early");
                    break;
                }
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

                bool do_video = (v_sample < total_frames) &&
                                (v_pts <= a_pts || a_sample >= total_audio_frames);

                if (do_video) {
                    if (audio_prio) {
                        // Audio priority: wall-clock skip + short timeout send
                        if (v_pts > 0) {
                            int64_t wall_us = esp_timer_get_time() - demux_start_time;
                            if (wall_us - v_pts > kDemuxSkipThresholdUs &&
                                !is_sync_sample(tr, v_sample)) {
                                v_sample++;
                                v_skipped++;
                                continue;
                            }
                        }
                    }
                    if (v_bytes == 0 || v_bytes > kReadBufSize) {
                        v_sample++;
                        continue;
                    }
                    int64_t t0 = esp_timer_get_time();
                    if (f_pos != (int64_t)v_offset) {
                        v_seeks++;
                        lseek(v_fd, (off_t)v_offset, SEEK_SET);
                    } else {
                        v_seek_skips++;
                    }
                    if (read(v_fd, read_buf, v_bytes) != (ssize_t)v_bytes) {
                        ESP_LOGE(TAG, "Failed to read video frame %d", v_sample);
                        break;
                    }
                    f_pos = (int64_t)v_offset + v_bytes;
                    total_v_read_us += esp_timer_get_time() - t0;
                    int nal_size = build_annex_b_nal(nal_buf, kReadBufSize, read_buf, v_bytes);
                    if (nal_size <= 0) {
                        v_sample++;
                        continue;
                    }
                    t0 = esp_timer_get_time();
                    if (audio_prio) {
                        if (!send_video_frame(nal_buf, nal_size, v_pts)) {
                            total_v_send_us += esp_timer_get_time() - t0;
                            v_sample++;
                            v_skipped++;
                            continue;
                        }
                    } else {
                        if (!send_nal(nal_buf, nal_size, v_pts, false)) {
                            ESP_LOGE(TAG, "Failed to send video frame %d", v_sample);
                            break;
                        }
                    }
                    total_v_send_us += esp_timer_get_time() - t0;
                    v_sent++;
                    v_sample++;
                } else {
                    if (a_bytes == 0 || a_bytes > kReadBufSize) {
                        a_sample++;
                        continue;
                    }
                    int64_t t0 = esp_timer_get_time();
                    if (af_pos != (int64_t)a_offset) {
                        a_seeks++;
                        lseek(a_fd, (off_t)a_offset, SEEK_SET);
                    } else {
                        a_seek_skips++;
                    }
                    if (read(a_fd, read_buf, a_bytes) != (ssize_t)a_bytes) {
                        ESP_LOGE(TAG, "Failed to read audio frame %d", a_sample);
                        break;
                    }
                    af_pos = (int64_t)a_offset + a_bytes;
                    total_a_read_us += esp_timer_get_time() - t0;
                    t0 = esp_timer_get_time();
                    if (!send_audio(read_buf, a_bytes, a_pts)) {
                        total_a_send_us += esp_timer_get_time() - t0;
                        a_dropped++;
                        a_sample++;
                        continue;
                    }
                    total_a_send_us += esp_timer_get_time() - t0;
                    a_sent++;
                    a_sample++;
                }
            }
            if (v_skipped > 0) {
                ESP_LOGI(TAG, "Demux video frames skipped: %u / %u", v_skipped, total_frames);
            }
            ESP_LOGI(TAG, "Demux timing: v_read=%lldms a_read=%lldms v_send=%lldms a_send=%lldms",
                     total_v_read_us / 1000, total_a_read_us / 1000,
                     total_v_send_us / 1000, total_a_send_us / 1000);
            ESP_LOGI(TAG, "Demux counts: v_sent=%u v_skip=%u a_sent=%u a_drop=%u",
                     v_sent, v_skipped, a_sent, a_dropped);
            ESP_LOGI(TAG, "Demux seeks: v_seek=%u v_skip=%u a_seek=%u a_skip=%u",
                     v_seeks, v_seek_skips, a_seeks, a_seek_skips);
            if (separate_a_fd) {
                close(a_fd);
            }
        } else
#endif
        {
            // Video-only: always blocking (no real-time constraint)
            int64_t f_pos = -1;  // track file position to skip redundant fseeks
            for (unsigned sample = 0; sample < total_frames; sample++) {
                if (sync_.stop_requested) {
                    ESP_LOGI(TAG, "Stop requested, ending demux early");
                    break;
                }
                unsigned frame_bytes = 0;
                unsigned timestamp = 0;
                unsigned duration = 0;

                MP4D_file_offset_t offset = MP4D_frame_offset(&mp4, video_track, sample,
                                                               &frame_bytes, &timestamp, &duration);

                int64_t pts_us = (timescale > 0) ? (int64_t)timestamp * 1000000LL / timescale : 0;

                if (frame_bytes == 0 || frame_bytes > kReadBufSize) {
                    ESP_LOGW(TAG, "Frame %d: invalid size %d, skipping", sample, frame_bytes);
                    f_pos = -1;  // position unknown after skip
                    continue;
                }

                if (f_pos != (int64_t)offset) {
                    lseek(v_fd, (off_t)offset, SEEK_SET);
                }
                if (read(v_fd, read_buf, frame_bytes) != (ssize_t)frame_bytes) {
                    ESP_LOGE(TAG, "Failed to read frame %d", sample);
                    break;
                }
                f_pos = (int64_t)offset + frame_bytes;

                int nal_size = build_annex_b_nal(nal_buf, kReadBufSize, read_buf, frame_bytes);
                if (nal_size <= 0) {
                    ESP_LOGW(TAG, "Frame %d: AVCC to Annex B conversion failed", sample);
                    continue;
                }

                if (!send_nal(nal_buf, nal_size, pts_us, false)) {
                    ESP_LOGE(TAG, "Failed to send frame %d", sample);
                    break;
                }
            }
        }

        int64_t demux_wall_elapsed = esp_timer_get_time() - demux_wall_start;
        ESP_LOGI(TAG, "Demux finished: %lld ms wall time", demux_wall_elapsed / 1000);

        close(v_fd);
        psram_free(read_buf);
        psram_free(nal_buf);
        MP4D_close(&mp4);
    }

    send_eos();
}

}  // namespace mp4
