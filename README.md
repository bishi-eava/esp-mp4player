# ESP32-S3 MP4 Movie Player

ESP32-S3搭載の小型LCDボードで、SDカード上のMP4動画（H.264 Baseline Profile）を再生するプレーヤーです。WiFi AP内蔵で、スマホのブラウザから再生操作やファイル管理が可能です。

> **PSRAM必須:** H.264デコードバッファにPSRAMを使用するため、PSRAM搭載のESP32-S3ボードが必要です。

## 参考動画

実際の動作の様子はこちらをご覧ください。

https://youtube.com/shorts/kdLJf5c8VBU

## 主な機能

- SDカード上のMP4動画（H.264 Baseline + AAC）を再生
- WiFi AP内蔵 — スマホのブラウザから再生操作・ファイル管理
- プレイリスト管理 — `/playlist` フォルダ内のMP4を順次再生、サブフォルダ対応
- 音量調整 — Web UIスライダーでリアルタイム変更（SPK Base構成）
- A/V同期モード切替 — Audio Priority / Full Video
- 自動スケーリング — LCD以上のサイズの動画はアスペクト比維持で縮小表示
- ファイルブラウザ — アップロード・ダウンロード・削除・リネーム・フォルダ作成
- QRコード表示 — アイドル時にWiFi接続QRをLCDに表示

## 対応ボード

本プロジェクトは以下の機材でのみ動作確認を行っています。他の機材での動作は保証しません。

### M5Stack Atom S3R + SPK Base

| 項目 | 仕様 |
|---|---|
| 本体 | [M5Stack Atom S3R](https://docs.m5stack.com/en/core/AtomS3R) |
| MCU | ESP32-S3-PICO-1-N8R8 (Flash: 8MB, PSRAM: 8MB Octal SPI) |
| ディスプレイ | GC9107 IPS 128x128 |
| SDカード | SPI mode via [SPK Base](https://docs.m5stack.com/en/atom/Atomic%20SPK%20Base) TF slot (**CS=NC: PCB上でLow固定**) |
| 音声出力 | NS4168 I2S DAC (モノラル) via SPK Base |

> **Note:** この構成は Atom S3R 本体と SPK Base のペアです。SPK BaseにはSDカードスロットとNS4168 I2Sスピーカーが搭載されており、音声付きMP4の再生が可能です。

### M5Stack Atom S3R + ATOMIC TF Card Reader

| 項目 | 仕様 |
|---|---|
| 本体 | [M5Stack Atom S3R](https://docs.m5stack.com/en/core/AtomS3R) |
| MCU | ESP32-S3-PICO-1-N8R8 (Flash: 8MB, PSRAM: 8MB Octal SPI) |
| ディスプレイ | GC9107 IPS 128x128 |
| SDカード | SPI mode via [ATOMIC TF Card Reader](https://docs.m5stack.com/en/atom/Atomic%20TF-Card%20Reader) |
| 音声出力 | なし |

> **Note:** この構成は Atom S3R 本体と ATOMIC TF Card Reader Base のペアです。音声出力には対応していません。

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

## WiFi ファイルサーバー

起動と同時にWiFi APが立ち上がり、スマホやPCのブラウザから操作できます。

| 項目 | 値 |
|---|---|
| SSID | `esp-mp4player` |
| パスワード | `12345678` |
| URL | `http://192.168.4.1` |

アイドル時はLCDにWiFi接続用のQRコードが表示されます。スマホのカメラで読み取ると自動接続できます。

### プレイヤーページ (`/player`)

- 再生 / 停止 / 次 / 前
- 音量スライダー (0-100%、ドラッグ中もリアルタイム反映)
- A/V同期モード切替 (Audio Priority / Full Video)
- プレイリスト表示・フォルダ選択

### ファイルブラウザページ (`/browse`)

- ファイル一覧・フォルダナビゲーション
- アップロード（再生停止中のみ、メモリ制約のため）
- ダウンロード・削除・リネーム・フォルダ作成
- ストレージ使用量表示

## アーキテクチャ

C++ クラスベース + FreeRTOS タスク構成。全コードは `namespace mp4` に配置されています。

```
映像: SDカード → DemuxStage (minimp4) → AVCC→Annex B変換 → DecodeStage (esp-h264 + YUV→RGB565) → DisplayStage (LovyanGFX DMA)
音声: SDカード → DemuxStage (minimp4) → AudioPipeline (esp_audio_codec AAC → Volume → I2S DMA)  ※BOARD_HAS_AUDIO時のみ
```

### クラス構成

| クラス | 役割 | タスク |
|---|---|---|
| `MediaController` | プレイリスト管理・再生制御・音量管理 | — |
| `FileServer` | WiFi AP + HTTP server + REST API | — |
| `Mp4Player` | オーケストレーター。共有状態の所有とタスク起動 | — |
| `DemuxStage` | SD I/O + MP4 demux + 映像/音声フレームのキュー送信 | Core 1, prio 4, 32KB |
| `DecodeStage` | H.264 decode + YUV→RGB565変換 + スケーリング | Core 1, prio 5, 48KB |
| `DisplayStage` | DoubleBuffer から LCD への SPI DMA 転送 | Core 0, prio 6, 4KB |
| `AudioPipeline` | AAC decode + ボリュームスケーリング + I2S DMA 出力 | Core 0, prio 7, 20KB |

### 共有状態（旧 `player_ctx_t` を分割）

| 構造体/クラス | 内容 |
|---|---|
| `VideoInfo` | 動画解像度、スケーリング後サイズ、表示オフセット |
| `PipelineSync` | NAL/Audio キュー、セマフォ、EOS フラグ、audio_volume |
| `DoubleBuffer` | RGB565 ダブルバッファ（PSRAM、swap/read/write 管理） |
| `AudioInfo` | サンプルレート、チャンネル数、AAC DSI |

### FreeRTOS タスク構成

```
app_main (Core 0):
  1. init_sdcard()          ← SD を先に初期化（SPIバス競合回避）
  2. init_display()         ← Display を後から初期化
  3. FileServer::start()    ← WiFi AP + HTTP server 起動（常時ON）
  4. MediaController        → プレイリスト管理 + 自動再生
  5. メインループ            → controller.tick()

Core 1                                Core 0
┌──────────────────┐
│ DemuxStage       │
│ SD + minimp4     │
│ prio=4, 32KB     │
│ video + audio    │
└───┬──────────┬───┘
    │          │
 nal_queue  audio_queue (BOARD_HAS_AUDIO時のみ)
    │          │
┌───▼──────┐ ┌─▼─────────────────┐
│DecodeStage│ │ AudioPipeline      │
│H.264+YUV │ │ AAC decode + I2S   │
│prio=5,48KB│ │ prio=7, 20KB       │
│ Core 1    │ │ Core 0             │
└───┬───────┘ └────────────────────┘
    │ DoubleBuffer
┌───▼──────────────┐
│ DisplayStage     │
│ pushImage (DMA)  │
│ prio=6, 4KB      │
│ Core 0           │
└──────────────────┘
```

- **ダブルバッファ同期:** DecodeStage がフレーム N+1 をデコード中に DisplayStage がフレーム N を DMA 転送
  - `decode_ready` セマフォ: decode完了 → display開始
  - `display_done` セマフォ: display完了 → decode次フレーム可
- **スケーリング:** LCDより大きい動画はアスペクト比を維持してnearest-neighborで縮小表示（レターボックス/ピラーボックス）
  - デコード上限: 960x540 (Full HD半分)
  - YUV→RGB565変換時にインラインでスケーリング（追加バッファ不要）
  - LCD以下の動画はスケーリングなし（fast path）
- **音声再生 (BOARD_HAS_AUDIO時のみ):** DemuxStageが映像/音声フレームをPTS順にインターリーブ送信
  - AudioPipeline: AACフレームをesp_audio_codecでPCMデコード → ボリュームスケーリング → I2S DMA出力
  - I2Sクロックが自然にリアルタイム再生速度を制御（バックプレッシャー）
  - ボリューム制御: ソフトウェアPCMスケーリング `(sample * vol) >> 8`（Web UIからリアルタイム変更可能）

## 使用ライブラリ

| ライブラリ | 用途 | ライセンス | リンク |
|---|---|---|---|
| LovyanGFX v1.2.19 | ディスプレイドライバ (DMA SPI) | MIT / BSD-2-Clause | https://github.com/lovyan03/LovyanGFX |
| esp-h264-component v1.2.0 | H.264ソフトウェアデコーダ (SIMD最適化) | Espressif | https://github.com/espressif/esp-h264-component |
| minimp4 | MP4コンテナパーサー (シングルヘッダ) | CC0 (Public Domain) | https://github.com/lieff/minimp4 |
| esp_audio_codec v2.4.0+ | AACデコーダ (BOARD_HAS_AUDIO時のみ) | Espressif | https://github.com/espressif/esp-adf-libs |
| espressif/qrcode | QRコード生成 (WiFi接続QR表示) | MIT | https://components.espressif.com/components/espressif/qrcode |

## 開発環境

- **Framework:** ESP-IDF (v5.5.0)
- **ビルドツール:** PlatformIO (espressif32 @ ^6.5.0)

## ビルド・実行

```bash
# Atom S3R + SPK Base（音声出力対応）
pio run -e atoms3r_spk           # ビルド
pio run -e atoms3r_spk -t upload # アップロード

# Atom S3R + TF Card Reader
pio run -e atoms3r               # ビルド
pio run -e atoms3r -t upload     # アップロード

# SpotPear
pio run -e spotpear              # ビルド
pio run -e spotpear -t upload    # アップロード

# シリアルモニタ (115200bps)
pio device monitor
```

## 動画の準備

SDカードの `playlist` フォルダにMP4ファイルを配置してください。サブフォルダにも対応しています。

WiFi接続後、ブラウザのファイルブラウザページからアップロードすることもできます（再生停止中のみ）。

H.264 Baseline Profile が**必須**です（ソフトウェアデコーダの制限）。

LCDより大きい動画はアスペクト比を維持したまま自動で縮小表示されます（最大対応解像度: 960x540）。
ただし高解像度の動画はデコード負荷が高くコマ落ちするため、**320x240 程度への事前変換を推奨**します。

> **`-g 15` は必須です。** 15fpsで1秒ごとにキーフレームを挿入します。音声付き再生時のフレームスキップ後に映像が回復するために必要です。

### 推奨（320x240、アスペクト比維持）

```bash
ffmpeg -i input.mp4 \
  -vf "scale=320:-2,fps=15" \
  -c:v libx264 -profile:v baseline -level 3.0 -g 15 \
  -pix_fmt yuv420p video.mp4
```

### 音声付き（Atom S3R + SPK Base）

SPK Base 構成では AAC 音声付き MP4 の再生に対応しています。

```bash
# 320x240 + AAC音声
ffmpeg -i input.mp4 \
  -vf "scale=320:-2,fps=15" \
  -c:v libx264 -profile:v baseline -level 3.0 -g 15 \
  -pix_fmt yuv420p \
  -c:a aac -b:a 128k -ar 44100 -ac 1 \
  video.mp4
```

### LCD同サイズ（スケーリングなし）

```bash
# SpotPear (240x240)
ffmpeg -i input.mp4 \
  -vf "scale=240:240,fps=15" \
  -c:v libx264 -profile:v baseline -level 3.0 -g 15 \
  -pix_fmt yuv420p video.mp4

# Atom S3R (128x128)
ffmpeg -i input.mp4 \
  -vf "scale=128:128,fps=15" \
  -c:v libx264 -profile:v baseline -level 3.0 -g 15 \
  -pix_fmt yuv420p video.mp4
```

| パラメータ | 値 | 説明 |
|---|---|---|
| 推奨解像度 | 320x240 | デコード負荷とのバランスが良い推奨値 |
| 最大解像度 | 960x540 | デコード上限（コマ落ちする場合あり） |
| フレームレート | 15fps | デコード性能に合わせた推奨値 |
| コーデック | libx264 | H.264エンコーダ |
| プロファイル | Baseline | **必須** (SWデコーダの制限) |
| キーフレーム間隔 | `-g 15` | **必須** (1秒ごと、フレームスキップ後の映像回復に必要) |
| ピクセルフォーマット | yuv420p | I420形式 |
| 音声コーデック | AAC | AAC-LC (SPK Base構成のみ) |
| 音声チャンネル | モノラル (-ac 1) | NS4168はモノラルスピーカー |

## ピン配置

### M5Stack Atom S3R + SPK Base

#### SPI (ディスプレイ: SPI3_HOST, 3-wire)

| 信号 | GPIO |
|---|---|
| DC | 42 |
| CS | 14 |
| SCK | 15 |
| MOSI | 21 |
| RST | 48 |
| バックライト | I2C LEDドライバ (addr: 0x30, SDA: 45, SCL: 0) |

#### SPI (SDカード: SPI2_HOST, SPK Base)

| 信号 | GPIO |
|---|---|
| MOSI | 6 |
| MISO | 8 |
| SCK | 7 |
| CS | NC (PCB上でLow固定) |

#### I2S (NS4168 DAC, SPK Base)

| 信号 | GPIO |
|---|---|
| BCLK | 5 |
| LRCLK | 39 |
| DATA | 38 |

> **Note:** SPK Base の基板上のラベルは ESP32 (Atom Lite) の GPIO 番号で印刷されています。ESP32-S3 (Atom S3R) では物理的に同じピンでも GPIO 番号が異なるため、上記の値を使用してください。

### M5Stack Atom S3R + ATOMIC TF Card Reader

#### SPI (ディスプレイ: SPI3_HOST, 3-wire)

Atom S3R + SPK Base と同一です。

#### SPI (SDカード: SPI2_HOST, ATOMIC TF Card Reader)

| 信号 | GPIO |
|---|---|
| MOSI | 6 |
| MISO | 8 |
| SCK | 7 |
| CS | 4 |

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
