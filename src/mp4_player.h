#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "board_config.h"
#include "lcd_config.h"
#include "player_constants.h"
#include "psram_alloc.h"

#ifdef BOARD_HAS_AUDIO
#include "driver/i2s_std.h"
#endif

namespace mp4 {

// --- Message types ---

struct FrameMsg {
    uint8_t *data;       // NAL data (PSRAM, receiver frees)
    int      size;
    int64_t  pts_us;
    bool     is_sps_pps;
    bool     eos;
};

#ifdef BOARD_HAS_AUDIO
struct AudioMsg {
    uint8_t *data;       // AAC frame (PSRAM, receiver frees)
    int      size;
    int64_t  pts_us;
    bool     eos;
};
#endif

// --- Shared state structs ---

struct VideoInfo {
    int video_w    = 0;
    int video_h    = 0;
    int scaled_w   = 0;
    int scaled_h   = 0;
    int display_x  = 0;
    int display_y  = 0;
};

struct PipelineSync {
    QueueHandle_t     nal_queue     = nullptr;
    SemaphoreHandle_t decode_ready  = nullptr;
    SemaphoreHandle_t display_done  = nullptr;
    volatile bool     pipeline_eos  = false;

#ifdef BOARD_HAS_AUDIO
    QueueHandle_t     audio_queue   = nullptr;
    volatile bool     audio_eos     = false;
#endif

    bool init() {
        nal_queue    = xQueueCreate(kNalQueueDepth, sizeof(FrameMsg));
        decode_ready = xSemaphoreCreateBinary();
        display_done = xSemaphoreCreateBinary();
#ifdef BOARD_HAS_AUDIO
        audio_queue  = xQueueCreate(kAudioQueueDepth, sizeof(AudioMsg));
#endif
        return nal_queue && decode_ready && display_done;
    }
};

class DoubleBuffer {
public:
    bool init(int width, int height) {
        width_  = width;
        height_ = height;
        size_t count = width * height;
        bufs_[0] = psram_alloc<uint16_t>(count);
        bufs_[1] = psram_alloc<uint16_t>(count);
        return bufs_[0] && bufs_[1];
    }

    void deinit() {
        safe_free(bufs_[0]); bufs_[0] = nullptr;
        safe_free(bufs_[1]); bufs_[1] = nullptr;
    }

    ~DoubleBuffer() { deinit(); }

    uint16_t *write_buf() { return bufs_[write_idx_]; }
    uint16_t *read_buf()  { return bufs_[read_idx_]; }
    void swap()           { write_idx_ ^= 1; read_idx_ ^= 1; }
    bool valid() const    { return bufs_[0] != nullptr && bufs_[1] != nullptr; }

private:
    uint16_t *bufs_[2] = {nullptr, nullptr};
    int write_idx_ = 0;
    int read_idx_  = 1;
    int width_  = 0;
    int height_ = 0;
};

#ifdef BOARD_HAS_AUDIO
struct AudioInfo {
    unsigned sample_rate  = 0;
    unsigned channels     = 0;
    uint8_t *dsi          = nullptr;
    unsigned dsi_bytes    = 0;

    ~AudioInfo() { safe_free(dsi); dsi = nullptr; }
};
#endif

// --- Stage classes ---

class DemuxStage {
public:
    DemuxStage(const char *filepath, PipelineSync &sync, VideoInfo &video_info
#ifdef BOARD_HAS_AUDIO
               , AudioInfo &audio_info
#endif
               )
        : filepath_(filepath), sync_(sync), video_info_(video_info)
#ifdef BOARD_HAS_AUDIO
        , audio_info_(audio_info)
#endif
    {}

    static void task_func(void *arg);

private:
    void run();
    bool send_nal(const uint8_t *data, int size, int64_t pts_us, bool is_sps_pps);
#ifdef BOARD_HAS_AUDIO
    bool send_audio(const uint8_t *data, int size, int64_t pts_us);
#endif
    void send_eos();

    static int  mp4_read_cb(int64_t offset, void *buffer, size_t size, void *token);
    static int  build_annex_b_nal(uint8_t *dst, int capacity, const uint8_t *src, int size);

    const char   *filepath_;
    PipelineSync &sync_;
    VideoInfo    &video_info_;
#ifdef BOARD_HAS_AUDIO
    AudioInfo    &audio_info_;
#endif
};

class DecodeStage {
public:
    DecodeStage(PipelineSync &sync, VideoInfo &video_info, DoubleBuffer &dbuf)
        : sync_(sync), video_info_(video_info), dbuf_(dbuf) {}

    static void task_func(void *arg);

private:
    void run();
    void compute_scaling(int video_w, int video_h);
    void drain_queue();

    PipelineSync &sync_;
    VideoInfo    &video_info_;
    DoubleBuffer &dbuf_;
};

class DisplayStage {
public:
    DisplayStage(PipelineSync &sync, VideoInfo &video_info, DoubleBuffer &dbuf, LGFX &display)
        : sync_(sync), video_info_(video_info), dbuf_(dbuf), display_(display) {}

    static void task_func(void *arg);

private:
    void run();

    PipelineSync &sync_;
    VideoInfo    &video_info_;
    DoubleBuffer &dbuf_;
    LGFX         &display_;
};

#ifdef BOARD_HAS_AUDIO
class AudioPipeline {
public:
    AudioPipeline(PipelineSync &sync, AudioInfo &audio_info)
        : sync_(sync), audio_info_(audio_info) {}

    static void task_func(void *arg);

private:
    void run();
    bool init_i2s(unsigned sample_rate, unsigned channels);
    void deinit_i2s();
    void drain_queue();

    PipelineSync &sync_;
    AudioInfo    &audio_info_;
    i2s_chan_handle_t tx_chan_ = nullptr;
};
#endif

// --- Orchestrator ---

class Mp4Player {
public:
    Mp4Player(LGFX &display, const char *filepath)
        : display_(display), filepath_(filepath) {}

    void start();

private:
    LGFX         &display_;
    const char   *filepath_;

    PipelineSync  sync_;
    VideoInfo     video_info_;
    DoubleBuffer  dbuf_;
#ifdef BOARD_HAS_AUDIO
    AudioInfo     audio_info_;
#endif
};

}  // namespace mp4
