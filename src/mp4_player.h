#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
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
    QueueHandle_t      nal_queue     = nullptr;
    SemaphoreHandle_t  decode_ready  = nullptr;
    SemaphoreHandle_t  display_done  = nullptr;
    EventGroupHandle_t task_done     = nullptr;
    volatile bool      pipeline_eos  = false;
    volatile bool      stop_requested = false;
    volatile bool      audio_priority = false;

    // Bits for task completion tracking via EventGroup
    static constexpr EventBits_t kDemuxDone   = (1 << 0);
    static constexpr EventBits_t kDecodeDone  = (1 << 1);
    static constexpr EventBits_t kDisplayDone = (1 << 2);
    static constexpr EventBits_t kAudioDone   = (1 << 3);
    static constexpr EventBits_t kAllDone     = kDemuxDone | kDecodeDone | kDisplayDone;
    static constexpr EventBits_t kAllDoneAudio = kAllDone | kAudioDone;

#ifdef BOARD_HAS_AUDIO
    QueueHandle_t     audio_queue   = nullptr;
    volatile bool     audio_eos     = false;
    volatile int      audio_volume  = 256;  // 0–256, 256=full volume
    volatile int32_t  audio_playback_pts_ms = -1;  // A/V sync: audio task reports playback position (ms, -1=not started)
#endif

    bool init() {
        pipeline_eos   = false;
        stop_requested = false;
        audio_priority = false;
#ifdef BOARD_HAS_AUDIO
        audio_eos      = false;
        audio_volume   = 256;
#endif
        nal_queue    = xQueueCreate(kNalQueueDepth, sizeof(FrameMsg));
        decode_ready = xSemaphoreCreateBinary();
        display_done = xSemaphoreCreateBinary();
        task_done    = xEventGroupCreate();
#ifdef BOARD_HAS_AUDIO
        audio_queue  = xQueueCreate(kAudioQueueDepth, sizeof(AudioMsg));
#endif
        return nal_queue && decode_ready && display_done && task_done;
    }

    void deinit() {
        if (nal_queue)    { vQueueDelete(nal_queue);         nal_queue    = nullptr; }
        if (decode_ready) { vSemaphoreDelete(decode_ready);  decode_ready = nullptr; }
        if (display_done) { vSemaphoreDelete(display_done);  display_done = nullptr; }
        if (task_done)    { vEventGroupDelete(task_done);    task_done    = nullptr; }
#ifdef BOARD_HAS_AUDIO
        if (audio_queue)  { vQueueDelete(audio_queue);       audio_queue  = nullptr; }
#endif
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
    bool send_video_frame(const uint8_t *data, int size, int64_t pts_us);
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

    void set_audio_priority(bool v) { audio_priority_ = v; }
    void set_volume(int vol) {
        volume_ = vol;
#ifdef BOARD_HAS_AUDIO
        sync_.audio_volume = vol * 256 / 100;
#endif
    }
    void start();
    void request_stop();
    bool is_finished() const;
    void wait_until_finished();

private:
    LGFX         &display_;
    const char   *filepath_;
    bool          audio_priority_ = true;
    int           volume_ = 100;

    PipelineSync  sync_;
    VideoInfo     video_info_;
    DoubleBuffer  dbuf_;
#ifdef BOARD_HAS_AUDIO
    AudioInfo     audio_info_;
#endif

    TaskHandle_t  demux_handle_   = nullptr;
    TaskHandle_t  decode_handle_  = nullptr;
    TaskHandle_t  display_handle_ = nullptr;
#ifdef BOARD_HAS_AUDIO
    TaskHandle_t  audio_handle_   = nullptr;
#endif
};

}  // namespace mp4
