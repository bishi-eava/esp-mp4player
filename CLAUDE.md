# ESP32-S3 MP4 Movie Player Project

## Supported Boards

### SpotPear ESP32-S3 LCD 1.3inch
- **MCU:** ESP32-S3 (esp32-s3-devkitm-1)
- **Flash:** 16MB, **PSRAM:** 8MB Octal SPI
- **Display:** ST7789 240x240 IPS, rotation=0
  - SPI Pins: DC=38, CS=39, SCK=40, MOSI=41, RST=42 (SPI2_HOST)
  - Backlight: GPIO20 (PWM), offset_x=0, offset_y=0, invert=true
- **SD Card:** SDMMC 1-bit mode (D0=16, D3=17, CMD=18, CLK=21)

### M5Stack Atom S3R + ATOMIC TF Card Reader
- **MCU:** ESP32-S3-PICO-1-N8R8 (board: m5stack-atoms3)
- **Flash:** 8MB, **PSRAM:** 8MB Octal SPI (専用内部SPI0/SPI1、GPIO消費なし)
- **Display:** GC9107 128x128 IPS, rotation=0
  - SPI Pins: DC=42, CS=14, SCK=15, MOSI=21, RST=48 (SPI3_HOST), spi_3wire=true
  - Backlight: I2C LEDドライバ (addr=0x30, SDA=45, SCL=0) + GPIO16 PWM
  - offset_x=0, offset_y=32, invert=false
  - **注意:** AtomS3(非R)とはSPIピンが異なる (SCK,DC,CS,RSTが全て別)
- **SD Card:** SPI mode via ATOMIC TF Card Reader (MOSI=6, MISO=8, SCK=7, CS=4, SPI2_HOST)

### M5Stack Atom S3R + SPK Base (NS4168 I2S Audio)
- **MCU:** ESP32-S3-PICO-1-N8R8 (board: m5stack-atoms3)
- **Flash:** 8MB, **PSRAM:** 8MB Octal SPI
- **Display:** GC9107 128x128 IPS (Atom S3Rと同一設定)
- **SD Card:** SPI mode via SPK Base TF slot (MOSI=6, MISO=8, SCK=7, CS=NC, SPI2_HOST)
  - **注意:** SPK BaseのCSピンはPCB上でLow固定、GPIO_NUM_NC (-1)
- **I2S Audio:** NS4168 DAC (BCLK=GPIO5, LRCLK=GPIO39, DATA=GPIO38)
  - **注意:** SPK Baseのラベルは ESP32 (Atom Lite) のGPIO番号で印刷されている。ESP32-S3では異なるGPIOにマッピングされる
- **パーティション:** 3MB app (esp_audio_codecライブラリにより1MB超)

## Architecture (FreeRTOS タスク構成)
```
app_main (Core 0):
  1. init_sdcard()  ← SD SPI を先に初期化（SPIバス競合回避）
  2. init_display() ← Display SPI を後から初期化
  3. nal_queue + audio_queue + セマフォ作成 → demux/decode/display/audio タスク起動

Core 1                                Core 0
┌──────────────────┐
│ demux_task       │
│ SD + minimp4     │
│ prio=4, 32KB     │
│ video + audio    │
└───┬──────────┬───┘
    │          │
 nal_queue  audio_queue (BOARD_HAS_AUDIO)
    │          │
┌───▼──────┐ ┌─▼─────────────────┐
│decode_task│ │ audio_task         │
│H.264+YUV │ │ AAC decode + I2S   │
│prio=5,48KB│ │ prio=7, 20KB       │
│ Core 1    │ │ Core 0             │
└───┬───────┘ └────────────────────┘
    │ double buffer
┌───▼──────┐
│display   │
│pushImage │
│prio=6,4KB│
│ Core 0   │
└──────────┘
```

### 音声パイプライン (BOARD_HAS_AUDIO)
- demux_taskが time-ordered interleaved demux でビデオ/オーディオフレームをPTS順に送信
- audio_task: audio_queueからAACフレーム受信 → esp_audio_codec でPCMデコード → I2S DMA出力
- I2Sクロックが自然にリアルタイム再生速度を制御（バックプレッシャー）
- ビデオはPTSベースのタイミング制御（既存）、両方リアルタイムクロックで近似同期

### スケーリング (Nearest-Neighbor)
- LCDより大きい動画はアスペクト比を維持して縮小表示（レターボックス/ピラーボックス）
- YUV→RGB565変換時にインラインでスケーリング（追加バッファ不要）
- デコード上限: 960x540 (Full HD半分、全ボード共通、`BOARD_MAX_DECODE_WIDTH/HEIGHT`)
- LCD以下の動画はスケーリングなし（fast path、既存動作と同じ）

### ダブルバッファ同期
- `decode_ready` セマフォ: decode完了 → display開始
- `display_done` セマフォ: display完了 → decode次フレーム可
- decode_task が frame N+1 をデコード中に display_task が frame N を DMA 転送

### 初期化順序（重要）
SD カード初期化を **必ずディスプレイより先に** 行うこと。
Atom S3R では Display(SPI3_HOST) → SD(SPI2_HOST) の順で初期化すると `TG1WDT_SYS_RST` でリブートする。

## Key Files
- `src/board_config.h` — ボード別GPIO・ディスプレイ・SDカード・I2S設定（条件コンパイル）
- `src/lcd_config.h` — LovyanGFX設定（board_config.hのマクロを使用）
- `src/main.cpp` — SD→Display初期化、nal_queue+audio_queue+セマフォ作成、タスク起動
- `src/mp4_player.h` — frame_msg_t, audio_msg_t, player_ctx_t 型定義、タスク宣言
- `src/mp4_player.cpp` — demux_task（SD I/O + MP4 demux + interleaved audio）、decode_task（H.264 decode + YUV→RGB565）、display_task（pushImage DMA転送）
- `src/audio_player.cpp` — audio_task（AAC decode + I2S output、BOARD_HAS_AUDIO時のみ）
- `src/yuv2rgb.h` — I420→RGB565変換（スケーリング付き `i420_to_rgb565_scaled()` 含む）
- `platformio.ini` — マルチ環境設定（spotpear/atoms3r/atoms3r_spk）
- `partitions_8MB.csv` — 8MB Flash用カスタムパーティション（3MB app）
- `sdkconfig.defaults.spotpear` — SpotPear用sdkconfig（Octal PSRAM, 16MB Flash）
- `sdkconfig.defaults.atoms3r` — Atom S3R用sdkconfig（Octal PSRAM, 8MB Flash）
- `sdkconfig.defaults.atoms3r_spk` — Atom S3R + SPK Base用sdkconfig

## Build
```bash
pio run -e spotpear              # SpotPearビルド
pio run -e atoms3r               # Atom S3Rビルド
pio run -e atoms3r_spk           # Atom S3R + SPK Baseビルド
pio run -e spotpear -t upload    # SpotPearへアップロード
pio run -e atoms3r -t upload     # Atom S3Rへアップロード
pio run -e atoms3r_spk -t upload # Atom S3R + SPK Baseへアップロード
pio device monitor               # シリアルモニタ (115200bps)
```

### クリーンビルド（sdkconfig変更時に必要）
```bash
rm -f sdkconfig.spotpear && rm -rf .pio/build/spotpear && pio run -e spotpear
rm -f sdkconfig.atoms3r && rm -rf .pio/build/atoms3r && pio run -e atoms3r
rm -f sdkconfig.atoms3r_spk && rm -rf .pio/build/atoms3r_spk && pio run -e atoms3r_spk
```
**重要:** `sdkconfig.defaults.*` を変更した場合、生成された `sdkconfig.<env>` を削除しないと反映されない

## Test Video Preparation
H.264 Baseline Profile 必須（SWデコーダ制限）。ファイル名: `/sdcard/video.mp4`
LCDより大きい動画はアスペクト比維持で自動縮小表示（最大対応 960x540）。高解像度はコマ落ちするため **320x240 推奨**。

```bash
# 320x240 推奨（アスペクト比維持、高さ自動+偶数丸め）
ffmpeg -i input.mp4 -vf "scale=320:-2,fps=15" -c:v libx264 -profile:v baseline -level 3.0 -pix_fmt yuv420p video.mp4

# 320x240 + AAC音声 — Atom S3R + SPK Base
ffmpeg -i input.mp4 -vf "scale=320:-2,fps=15" -c:v libx264 -profile:v baseline -level 3.0 -pix_fmt yuv420p -c:a aac -b:a 128k -ar 44100 -ac 1 video.mp4

# LCD同サイズ（スケーリングなし）— SpotPear
ffmpeg -i input.mp4 -vf "scale=240:240,fps=15" -c:v libx264 -profile:v baseline -level 3.0 -pix_fmt yuv420p video.mp4

# LCD同サイズ（スケーリングなし）— Atom S3R
ffmpeg -i input.mp4 -vf "scale=128:128,fps=15" -c:v libx264 -profile:v baseline -level 3.0 -pix_fmt yuv420p video.mp4
```

## Key Libraries
- **LovyanGFX:** ディスプレイドライバ（DMA SPI, ESP-IDF対応, ST7789/GC9107）
- **minimp4:** MP4コンテナパーサー（シングルヘッダ, CC0ライセンス）
- **esp-h264-component:** H.264ソフトウェアデコーダ（Espressif公式, SIMD最適化）
- **esp_audio_codec:** AACデコーダ（Espressif公式, AAC-LC/HE-AAC対応, BOARD_HAS_AUDIO時のみ）

## Reference
- サンプルコード: `../Reference/ESP32-S3-LCD-1.3-Demo-New/Arduino/`
- ライブラリ: `../Reference/ESP32-S3-LCD-1.3-Demo-New/lib/`
