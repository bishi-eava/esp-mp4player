#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lcd_config.h"

// demux_task → decode_task へ送る NAL データ
typedef struct {
    uint8_t *data;       // NAL data (PSRAM確保、受信側が解放)
    int      size;       // NAL data サイズ
    int64_t  pts_us;     // プレゼンテーションタイムスタンプ (microseconds)
    bool     is_sps_pps; // SPS/PPS パラメータセット
    bool     eos;        // ストリーム終了マーカー
} frame_msg_t;

// タスク間で共有するコンテキスト
typedef struct {
    const char    *filepath;
    LGFX          *display;
    QueueHandle_t  nal_queue;        // demux → decode

    // demux_task が設定 (MP4メタデータから)
    int            video_w;
    int            video_h;

    // decode ↔ display ダブルバッファ同期
    SemaphoreHandle_t decode_ready;  // decode完了 → display開始
    SemaphoreHandle_t display_done;  // display完了 → decode次フレーム可
    uint16_t      *rgb_buf[2];      // ダブルバッファ (PSRAM, decode_task内で確保)
    volatile int   active_buf;      // display_task が読むバッファindex
    int            display_x;       // 表示オフセットX
    int            display_y;       // 表示オフセットY
    volatile bool  pipeline_eos;    // EOS フラグ (decode→display)

    // [将来] QueueHandle_t audio_queue;
} player_ctx_t;

// タスクエントリーポイント
void demux_task(void *arg);
void decode_task(void *arg);
void display_task(void *arg);
