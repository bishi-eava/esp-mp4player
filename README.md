# ESP32-S3 MP4 Movie Player

ESP32-S3搭載の小型LCDボードで、SDカード上のMP4動画（H.264 Baseline Profile）を再生するプレーヤーです。

## 参考動画

実際の動作の様子はこちらをご覧ください。

https://youtube.com/shorts/kdLJf5c8VBU

## 使用機材

本プロジェクトは以下の機材でのみ動作確認を行っています。他の機材での動作は保証しません。

| 項目 | 仕様 |
|---|---|
| ボード | SpotPear ESP32-S3 LCD 1.3inch |
| MCU | ESP32-S3 (PSRAM: Octal SPI, 80MHz) |
| ディスプレイ | ST7789 IPS 240x240 |
| SDカード | SDMMC 1-bitモード |
| IMUセンサー | QMI8658 (I2C) ※本プロジェクトでは未使用 |

- Wiki: https://spotpear.com/wiki/ESP32-S3-1.3-inch-LCD-ST7789-240x240-Display-Screen.html
- Shop: https://spotpear.com/shop/ESP32-S3-1.3-inch-LCD-ST7789-240x240-Display-Screen.html

## アーキテクチャ

```
SDカード → minimp4 (demux) → AVCC→Annex B変換 → esp-h264 (decode) → I420→RGB565変換 → LovyanGFX (display)
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
- **ボード設定:** esp32-s3-devkitm-1

## ビルド・実行

```bash
# ビルド
pio run

# ビルド＆アップロード
pio run -t upload

# シリアルモニタ (115200bps)
pio device monitor
```

## 動画の準備

ffmpegで再生用のMP4ファイルを変換し、SDカードに保存してください。

```bash
ffmpeg -i input.mp4 \
  -vf "scale=240:240,fps=15" \
  -c:v libx264 \
  -profile:v baseline \
  -level 3.0 \
  -pix_fmt yuv420p \
  output.mp4
```

| パラメータ | 値 | 説明 |
|---|---|---|
| 解像度 | 240x240 | ディスプレイに合わせる |
| フレームレート | 15fps | デコード性能に合わせた推奨値 |
| コーデック | libx264 | H.264エンコーダ |
| プロファイル | Baseline | **必須** (SWデコーダの制限) |
| レベル | 3.0 | Baseline Profileの標準レベル |
| ピクセルフォーマット | yuv420p | I420形式 |

変換したファイルを `video.mp4` としてSDカードのルートに保存してください。

## ピン配置

### SPI (ディスプレイ)

| 信号 | GPIO |
|---|---|
| DC | 38 |
| CS | 39 |
| SCK | 40 |
| MOSI | 41 |
| RST | 42 |
| バックライト | 20 |

### SDMMC

| 信号 | GPIO |
|---|---|
| D0 | 16 |
| D3 (CS) | 17 |
| CMD | 18 |
| CLK | 21 |

### I2C (IMU: QMI8658)

| 信号 | GPIO |
|---|---|
| SDA | 47 |
| SCL | 48 |
| INT1 | 46 |
| INT2 | 45 |
