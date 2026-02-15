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
constexpr int kDemuxCore   = 1;
constexpr int kDecodeCore  = 1;
constexpr int kDisplayCore = 0;
constexpr int kAudioCore   = 0;

// --- Queue depths ---
constexpr int kNalQueueDepth   = 4;
constexpr int kAudioQueueDepth = 8;

// --- Buffer sizes ---
constexpr size_t kReadBufSize  = 64 * 1024;
constexpr size_t kStdioBufSize =  8 * 1024;
constexpr size_t kPcmBufSize   = 1024 * 2 * sizeof(int16_t);  // 4096 bytes

// --- SD card mount config ---
constexpr int    kSdMaxFiles       = 5;
constexpr size_t kSdAllocUnitSize  = 16 * 1024;

// --- I2S DMA config ---
constexpr int kI2sDmaDescNum  = 4;
constexpr int kI2sDmaFrameNum = 512;

// --- Timeout durations (ms) ---
constexpr int kQueueSendTimeoutMs  = 5000;
constexpr int kQueueRecvTimeoutMs  = 10000;
constexpr int kAudioRecvTimeoutMs  = 5000;
constexpr int kSemaphoreTimeoutMs  = 10000;
constexpr int kFinalDisplayWaitMs  = 1000;
constexpr int kBootDelayMs         = 5000;
constexpr int kSplashDelayMs       = 500;

// --- Default file path ---
constexpr const char *kMp4FilePath = "/sdcard/video.mp4";

}  // namespace mp4
