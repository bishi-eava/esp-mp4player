# ESP32-S3 MP4 Movie Player

ESP32-S3搭載の小型LCDボードで、SDカード上のMP4動画（H.264 Baseline Profile）を再生するプレーヤーです。

> **PSRAM必須:** H.264デコードバッファにPSRAMを使用するため、PSRAM搭載のESP32-S3ボードが必要です。

## 参考動画

実際の動作の様子はこちらをご覧ください。

https://youtube.com/shorts/kdLJf5c8VBU

## 対応ボード

本プロジェクトは以下の機材でのみ動作確認を行っています。他の機材での動作は保証しません。

### SpotPear ESP32-S3 LCD 1.3inch

| 項目 | 仕様 |
|---|---|
| ボード | SpotPear ESP32-S3 LCD 1.3inch |
| MCU | ESP32-S3 (Flash: 16MB, PSRAM: 8MB Octal SPI) |
| ディスプレイ | ST7789 IPS 240x240 |
| SDカード | SDMMC 1-bitモード (オンボード) |
| 音声出力 | なし |

- Wiki: https://spotpear.com/wiki/ESP32-S3-1.3-inch-LCD-ST7789-240x240-Display-Screen.html
- Shop: https://spotpear.com/shop/ESP32-S3-1.3-inch-LCD-ST7789-240x240-Display-Screen.html

### M5Stack Atom S3R + ATOMIC TF Card Reader

| 項目 | 仕様 |
|---|---|
| 本体 | [M5Stack Atom S3R](https://docs.m5stack.com/en/core/AtomS3R) |
| MCU | ESP32-S3-PICO-1-N8R8 (Flash: 8MB, PSRAM: 8MB Octal SPI) |
| ディスプレイ | GC9107 IPS 128x128 |
| SDカード | SPI mode via [ATOMIC TF Card Reader](https://docs.m5stack.com/en/atom/Atomic%20TF-Card%20Reader) |
| 音声出力 | なし |

> **Note:** この構成は Atom S3R 本体と ATOMIC TF Card Reader Base のペアです。
> 今後、Atom S3R + ATOMIC SPK Base（スピーカー付き、音声出力対応）の構成も追加予定です。

## アーキテクチャ

```
SDカード → minimp4 (demux) → AVCC→Annex B変換 → esp-h264 (decode) → I420→RGB565変換 → LovyanGFX (display)
```

### FreeRTOS タスク構成

```
app_main:
  1. init_sdcard()  ← SD を先に初期化（SPIバス競合回避）
  2. init_display() ← Display を後から初期化
  3. FreeRTOS queue 作成 → demux_task + video_task 起動

demux_task (Core 1): SD読み込み → minimp4 demux → AVCC→AnnexB → queue送信
video_task (Core 0): queue受信 → esp-h264 decode → I420→RGB565 → LovyanGFX display
```

## 使用ライブラリ

| ライブラリ | 用途 | ライセンス | リンク |
|---|---|---|---|
| LovyanGFX v1.2.19 | ディスプレイドライバ (DMA SPI) | MIT / BSD-2-Clause | https://github.com/lovyan03/LovyanGFX |
| esp-h264-component v1.2.0 | H.264ソフトウェアデコーダ (SIMD最適化) | Espressif | https://github.com/espressif/esp-h264-component |
| minimp4 | MP4コンテナパーサー (シングルヘッダ) | CC0 (Public Domain) | https://github.com/lieff/minimp4 |

## 開発環境

- **Framework:** ESP-IDF (v5.5.0)
- **ビルドツール:** PlatformIO (espressif32 @ ^6.5.0)

## ビルド・実行

```bash
# SpotPear
pio run -e spotpear              # ビルド
pio run -e spotpear -t upload    # アップロード

# Atom S3R + TF Card Reader
pio run -e atoms3r               # ビルド
pio run -e atoms3r -t upload     # アップロード

# シリアルモニタ (115200bps)
pio device monitor
```

## 動画の準備

ffmpegで再生用のMP4ファイルを変換し、SDカードのルートに `video.mp4` として保存してください。

H.264 Baseline Profile が**必須**です（ソフトウェアデコーダの制限）。

### SpotPear (240x240)

```bash
ffmpeg -i input.mp4 \
  -vf "scale=240:240,fps=15" \
  -c:v libx264 -profile:v baseline -level 3.0 \
  -pix_fmt yuv420p video.mp4
```

### Atom S3R (128x128)

```bash
ffmpeg -i input.mp4 \
  -vf "scale=128:128,fps=15" \
  -c:v libx264 -profile:v baseline -level 3.0 \
  -pix_fmt yuv420p video.mp4
```

| パラメータ | 値 | 説明 |
|---|---|---|
| フレームレート | 15fps | デコード性能に合わせた推奨値 |
| コーデック | libx264 | H.264エンコーダ |
| プロファイル | Baseline | **必須** (SWデコーダの制限) |
| ピクセルフォーマット | yuv420p | I420形式 |

## ピン配置

### SpotPear ESP32-S3 LCD 1.3inch

#### SPI (ディスプレイ: SPI2_HOST)

| 信号 | GPIO |
|---|---|
| DC | 38 |
| CS | 39 |
| SCK | 40 |
| MOSI | 41 |
| RST | 42 |
| バックライト | 20 (PWM) |

#### SDMMC

| 信号 | GPIO |
|---|---|
| D0 | 16 |
| D3 (CS) | 17 |
| CMD | 18 |
| CLK | 21 |

### M5Stack Atom S3R + ATOMIC TF Card Reader

#### SPI (ディスプレイ: SPI3_HOST, 3-wire)

| 信号 | GPIO |
|---|---|
| DC | 42 |
| CS | 14 |
| SCK | 15 |
| MOSI | 21 |
| RST | 48 |
| バックライト | I2C LEDドライバ (addr: 0x30, SDA: 45, SCL: 0) |

#### SPI (SDカード: SPI2_HOST, ATOMIC TF Card Reader)

| 信号 | GPIO |
|---|---|
| MOSI | 6 |
| MISO | 8 |
| SCK | 7 |
| CS | 4 |
