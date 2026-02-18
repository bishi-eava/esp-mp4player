#pragma once

#include <cstddef>

namespace mp4 {

// --- Task stack sizes (bytes) ---
constexpr size_t kDemuxStackSize   = 32 * 1024;
constexpr size_t kDecodeStackSize  = 48 * 1024;
constexpr size_t kDisplayStackSize =  4 * 1024;
constexpr size_t kAudioStackSize   = 20 * 1024;

// --- Task priorities ---
constexpr int kDemuxPriority   = 4;
constexpr int kDecodePriority  = 5;
constexpr int kDisplayPriority = 6;
constexpr int kAudioPriority   = 7;

// --- Core affinity ---
constexpr int kDemuxCore   = 0;
constexpr int kDecodeCore  = 1;
constexpr int kDisplayCore = 0;
constexpr int kAudioCore   = 0;

// --- Queue depths ---
constexpr int kNalQueueDepth   = 16;
constexpr int kAudioQueueDepth = 16;

// --- Buffer sizes ---
constexpr size_t kReadBufSize  = 64 * 1024;
constexpr size_t kStdioBufSize =  8 * 1024;
constexpr size_t kPcmBufSize   = 1024 * 2 * sizeof(int16_t);  // 4096 bytes

// --- SD card mount config ---
constexpr int    kSdMaxFiles       = 6;
constexpr size_t kSdAllocUnitSize  = 16 * 1024;

// --- I2S DMA config ---
constexpr int kI2sDmaDescNum  = 4;
constexpr int kI2sDmaFrameNum = 512;

// --- Timeout durations (ms) ---
constexpr int kQueueSendTimeoutMs  = 5000;
constexpr int kVideoSendTimeoutMs  = 100;   // demux: wait up to 100ms for queue space before skipping
constexpr int kAudioSendTimeoutMs  = 200;   // demux: audio queue send timeout (shorter than generic 5s)
constexpr int kQueueRecvTimeoutMs  = 10000;
constexpr int kAudioRecvTimeoutMs  = 5000;

// --- Frame skip (A/V sync) ---
constexpr int64_t kDemuxSkipThresholdUs = 200000;  // demux: skip SD read if >200ms behind wall clock
constexpr int kSemaphoreTimeoutMs  = 10000;
constexpr int kFinalDisplayWaitMs  = 1000;
constexpr int kBootDelayMs         = 5000;
constexpr int kSplashDelayMs       = 500;
constexpr int kQrCycleIntervalTicks = 10;  // 500ms * 10 = 5秒
constexpr int kQrScreenCount        = 3;

// --- SD card paths ---
constexpr const char *kSdMountPoint = "/sdcard";
constexpr const char *kPlaylistFolder = "/playlist";

// --- WiFi AP config ---
constexpr const char *kApSsid       = "esp-mp4player";
constexpr const char *kApPassword   = "12345678";
constexpr int kApChannel            = 1;
constexpr int kApMaxConnections     = 2;

// --- HTTP server config ---
constexpr size_t kHttpServerStack   = 8 * 1024;
constexpr size_t kHttpScratchSize   = 8 * 1024;
constexpr int kHttpMaxUriHandlers   = 22;

}  // namespace mp4
