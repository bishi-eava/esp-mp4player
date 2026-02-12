#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lcd_config.h"

// demux_task → video_task へ送るフレームデータ
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
    QueueHandle_t  video_queue;   // demux → video
    int            video_w;       // demux_task が設定 (MP4メタデータから)
    int            video_h;       // demux_task が設定
    // [将来] QueueHandle_t audio_queue;
} player_ctx_t;

// タスクエントリーポイント
void demux_task(void *arg);
void video_task(void *arg);
