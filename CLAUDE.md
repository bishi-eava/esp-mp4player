# ESP32-S3 MP4 Movie Player Project

## Board
- **Name:** SpotPear ESP32-S3 LCD 1.3inch
- **Wiki:** https://spotpear.com/wiki/ESP32-S3-1.3-inch-LCD-ST7789-240x240-Display-Screen.html
- **MCU:** ESP32-S3 (esp32-s3-devkitm-1)
- **Framework:** ESP-IDF (PlatformIO, espressif32 @ ^6.5.0)
- **PSRAM:** あり（Octal SPI, 80MHz）

## Display
- **Controller:** ST7789 (IPS)
- **Resolution:** 240x240
- **SPI Pins:** DC=38, CS=39, SCK=40, MOSI=41, RST=42 (SPI2_HOST)
- **Backlight:** GPIO20 (PWM, HIGH=ON)
- **SPI Frequency:** 40MHz
- **Library:** LovyanGFX（設定は `src/lcd_config.h`）
- **Note:** offset_y=80, invert=true (IPS panel)

## SD Card (SDMMC 1-bit mode)
- D0=16, D3(CS)=17, CMD=18, CLK=21
- D3をHIGHに設定してからSDMMC初期化
- ESP-IDFの `sdmmc_host` + `esp_vfs_fat` API を使用
- マウントポイント: `/sdcard`

## IMU Sensor (QMI8658)
- I2C: SDA=47, SCL=48
- INT1=46, INT2=45

## Architecture (MP4 Player Pipeline)
```
SDカード → minimp4 (demux) → AVCC→Annex B変換 → esp-h264 (decode) → I420→RGB565変換 → LovyanGFX (display)
```

## Key Libraries
- **LovyanGFX:** ディスプレイドライバ（DMA SPI, ESP-IDF対応）
- **minimp4:** MP4コンテナパーサー（シングルヘッダ, CC0ライセンス）
- **esp-h264-component:** H.264ソフトウェアデコーダ（Espressif公式, SIMD最適化）

## Build
- `pio run` でビルド
- `pio run -t upload` でアップロード
- `pio device monitor` でシリアルモニタ (115200bps)

## Test Video Preparation
```bash
ffmpeg -i input.mp4 -vf "scale=240:240,fps=15" -c:v libx264 -profile:v baseline -level 3.0 -pix_fmt yuv420p /sdcard/video.mp4
```
- Baseline Profile 必須（SWデコーダ制限）
- ファイル名: `/sdcard/video.mp4`

## Reference
- サンプルコード: `../Reference/ESP32-S3-LCD-1.3-Demo-New/Arduino/`
- ライブラリ: `../Reference/ESP32-S3-LCD-1.3-Demo-New/lib/`
