#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "board_config.h"
#include "lcd_config.h"

// demux_task → decode_task へ送る NAL データ
typedef struct {
    uint8_t *data;       // NAL data (PSRAM確保、受信側が解放)
    int      size;       // NAL data サイズ
    int64_t  pts_us;     // プレゼンテーションタイムスタンプ (microseconds)
    bool     is_sps_pps; // SPS/PPS パラメータセット
    bool     eos;        // ストリーム終了マーカー
} frame_msg_t;

#ifdef BOARD_HAS_AUDIO
// demux_task → audio_task へ送る AAC フレームデータ
typedef struct {
    uint8_t *data;       // AAC frame data (PSRAM確保, 受信側が解放)
    int      size;       // AAC frame サイズ
    int64_t  pts_us;     // プレゼンテーションタイムスタンプ (microseconds)
    bool     eos;        // ストリーム終了マーカー
} audio_msg_t;
#endif

// タスク間で共有するコンテキスト
typedef struct {
    const char    *filepath;
    LGFX          *display;
    QueueHandle_t  nal_queue;        // demux → decode

    // demux_task が設定 (MP4メタデータから)
    int            video_w;          // 元の動画解像度
    int            video_h;
    int            scaled_w;         // スケーリング後の表示幅
    int            scaled_h;         // スケーリング後の表示高さ

    // decode ↔ display ダブルバッファ同期
    SemaphoreHandle_t decode_ready;  // decode完了 → display開始
    SemaphoreHandle_t display_done;  // display完了 → decode次フレーム可
    uint16_t      *rgb_buf[2];      // ダブルバッファ (PSRAM, decode_task内で確保)
    volatile int   active_buf;      // display_task が読むバッファindex
    int            display_x;       // 表示オフセットX
    int            display_y;       // 表示オフセットY
    volatile bool  pipeline_eos;    // EOS フラグ (decode→display)

#ifdef BOARD_HAS_AUDIO
    // Audio pipeline (demux → audio_task)
    QueueHandle_t  audio_queue;
    unsigned       audio_sample_rate;
    unsigned       audio_channels;
    uint8_t       *audio_dsi;       // AAC AudioSpecificConfig (コピー)
    unsigned       audio_dsi_bytes;
    volatile bool  audio_eos;
#endif
} player_ctx_t;

// タスクエントリーポイント
void demux_task(void *arg);
void decode_task(void *arg);
void display_task(void *arg);
#ifdef BOARD_HAS_AUDIO
void audio_task(void *arg);
#endif
void monitor_task(void *arg);
