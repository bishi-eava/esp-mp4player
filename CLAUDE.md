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

## Architecture (C++ クラスベース + FreeRTOS タスク)

全コードは `namespace mp4` に配置。各タスクは Stage クラスとして実装（`static task_func()` → `run()` トランポリンパターン）。

```
app_main (Core 0):
  1. init_sdcard()    ← SD SPI を先に初期化（SPIバス競合回避）
  2. init_display()   ← Display SPI を後から初期化
  3. FileServer::start() ← WiFi AP + HTTP server 起動（常時ON）
  4. MediaController::scan_playlist() → SD上の.mp4ファイル列挙
  5. 自動再生 or アイドル → メインループ (controller.tick())

Core 1                                Core 0
┌──────────────────┐
│ DemuxStage       │
│ SD + minimp4     │
│ prio=4, 32KB     │
│ video + audio    │
└───┬──────────┬───┘
    │          │
 nal_queue  audio_queue (BOARD_HAS_AUDIO)
    │          │
┌───▼──────┐ ┌─▼─────────────────┐
│DecodeStage│ │ AudioPipeline      │
│H.264+YUV │ │ AAC decode + I2S   │
│prio=5,48KB│ │ prio=7, 20KB       │
│ Core 1    │ │ Core 0             │
└───┬───────┘ └────────────────────┘
    │ DoubleBuffer
┌───▼──────┐
│DisplayStage│
│pushImage │
│prio=6,4KB│
│ Core 0   │
└──────────┘
```

### 共有状態の分割（旧 player_ctx_t を廃止）
| 構造体/クラス | 内容 |
|---|---|
| `VideoInfo` | video_w/h, scaled_w/h, display_x/y |
| `PipelineSync` | nal_queue, audio_queue, セマフォ, EventGroup(task_done), EOSフラグ, stop_requested, audio_volume |
| `DoubleBuffer` | RGB565ダブルバッファ（PSRAM, swap/read/write） |
| `AudioInfo` | sample_rate, channels, DSI |

### 再生ライフサイクル管理
- `MediaController` がプレイリスト(SD上の.mp4)と再生状態を管理
- **閲覧/再生状態の分離**: `current_folder_`/`playlist_` は閲覧中のフォルダ、`playing_folder_`/`playing_file_` は再生中のファイルを保持。フォルダ切替で閲覧状態が変わっても再生状態は保持される
- **コマンドキューパターン**: HTTPハンドラは `post_play()`/`post_stop()` 等でキューに投入、`tick()` がメインスレッドで一括処理
  - `player_` へのアクセスはメインスレッドに集約（use-after-free/double-free防止）
- `Mp4Player` に `request_stop()` / `is_finished()` / `wait_until_finished()` メソッド
- `PipelineSync.stop_requested` フラグで **全ステージ**（Demux/Decode/Display/Audio）のループを中断
- **EventGroup タスク完了追跡**: 各タスクが `xEventGroupSetBits()` → `wait_until_finished()` が `xEventGroupWaitBits()` で全タスク完了を保証
- `send_eos()` は200msタイムアウト（停止中にキュー満杯でも無期限ブロックしない）
- Stage オブジェクトは `task_func()` 内で `delete self` して自己解放

### 音声パイプライン (BOARD_HAS_AUDIO)
- demux_taskが time-ordered interleaved demux でビデオ/オーディオフレームをPTS順に送信
- audio_task: audio_queueからAACフレーム受信 → esp_audio_codec でPCMデコード → ボリュームスケーリング → I2S DMA出力
- I2Sクロックが自然にリアルタイム再生速度を制御（バックプレッシャー）
- ビデオはPTSベースのタイミング制御（既存）、両方リアルタイムクロックで近似同期
- **ボリューム制御**: ソフトウェアPCMスケーリング `(sample * vol) >> 8`（固定小数点、vol: 0–256）
  - Web UI slider → POST /api/volume → MediaController → Mp4Player → PipelineSync.audio_volume (volatile)
  - 3パス最適化: vol==0→memset(0), vol<256→スケーリング, vol==256→no-op

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
- `src/player_constants.h` — タスクスタック/優先度/キュー深度/バッファサイズ/WiFi/HTTP定数
- `src/psram_alloc.h` — PSRAM/内部RAM確保ヘルパー（`psram_alloc<T>()`, `psram_free()` 等）
- `src/main.cpp` — SD→Display→WiFi→MediaController初期化、メインループ
- `src/mp4_player.h` — namespace mp4: FrameMsg, AudioMsg, VideoInfo, PipelineSync, DoubleBuffer, AudioInfo, Stage/Pipeline クラス宣言, Mp4Player
- `src/media_controller.h/cpp` — MediaController: playlistフォルダベースのプレイリスト管理、サブフォルダ選択、自動次再生、再生/停止/次/前/tick/音量
- `src/wifi_file_server.h/cpp` — FileServer: WiFi AP + HTTP server + REST API + ファイル管理
- `src/html_content.h` — Web UI SPA (HTML/CSS/JS、const char[] raw string literal)
- `src/qr_display.h` — QRコード生成+LovyanGFX描画（espressif/qrcode、WiFi接続QR表示）
- `src/demux_task.cpp` — DemuxStage クラス実装（SD I/O + MP4 demux + interleaved audio）
- `src/decode_task.cpp` — DecodeStage クラス実装（H.264 decode + YUV→RGB565 + スケーリング）
- `src/display_task.cpp` — DisplayStage クラス実装（pushImage DMA転送）
- `src/audio_player.cpp` — AudioPipeline クラス実装（AAC decode + ボリュームスケーリング + I2S output、BOARD_HAS_AUDIO時のみ）
- `src/yuv2rgb.h` — namespace mp4: BT.601 YUV→RGB565変換（`yuv_to_rgb565()` 単一コア + scaled/unscaled API）
- `platformio.ini` — マルチ環境設定（spotpear/atoms3r/atoms3r_spk、全環境3MB app partition）
- `partitions_8MB.csv` — カスタムパーティション（3MB app + 残りdata）
- `sdkconfig.defaults.spotpear` — SpotPear用sdkconfig（Octal PSRAM, 16MB Flash, WiFi最適化）
- `sdkconfig.defaults.atoms3r` — Atom S3R用sdkconfig（Octal PSRAM, 8MB Flash, WiFi最適化）
- `sdkconfig.defaults.atoms3r_spk` — Atom S3R + SPK Base用sdkconfig（WiFi最適化）

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
H.264 Baseline Profile 必須（SWデコーダ制限）。動画は `/sdcard/playlist/` フォルダに配置する。
LCDより大きい動画はアスペクト比維持で自動縮小表示（最大対応 960x540）。高解像度はコマ落ちするため **320x240 推奨**。
**`-g 15` 必須**: 15fpsで1秒ごとにキーフレーム挿入。音声付き再生時のフレームスキップ後の映像回復に必要。

```bash
# 320x240 推奨（アスペクト比維持、高さ自動+偶数丸め）
ffmpeg -i input.mp4 -vf "scale=320:-2,fps=15" -c:v libx264 -profile:v baseline -level 3.0 -g 15 -pix_fmt yuv420p video.mp4

# 320x240 + AAC音声 — Atom S3R + SPK Base
ffmpeg -i input.mp4 -vf "scale=320:-2,fps=15" -c:v libx264 -profile:v baseline -level 3.0 -g 15 -pix_fmt yuv420p -c:a aac -b:a 128k -ar 44100 -ac 1 video.mp4

# LCD同サイズ（スケーリングなし）— SpotPear
ffmpeg -i input.mp4 -vf "scale=240:240,fps=15" -c:v libx264 -profile:v baseline -level 3.0 -g 15 -pix_fmt yuv420p video.mp4

# LCD同サイズ（スケーリングなし）— Atom S3R
ffmpeg -i input.mp4 -vf "scale=128:128,fps=15" -c:v libx264 -profile:v baseline -level 3.0 -g 15 -pix_fmt yuv420p video.mp4
```

## Key Libraries
- **LovyanGFX:** ディスプレイドライバ（DMA SPI, ESP-IDF対応, ST7789/GC9107）
- **minimp4:** MP4コンテナパーサー（シングルヘッダ, CC0ライセンス）
- **esp-h264-component:** H.264ソフトウェアデコーダ（Espressif公式, SIMD最適化）
- **esp_audio_codec:** AACデコーダ（Espressif公式, AAC-LC/HE-AAC対応, BOARD_HAS_AUDIO時のみ）
- **espressif/qrcode:** QRコード生成（qrcodegen ベース、WiFi接続QRをLCDに表示）

## WiFi ファイルサーバー + ブラウザ操作

### 概要
WiFi AP + HTTP server が常時動作。スマホのブラウザから動画再生操作とファイル管理を行える。

### アーキテクチャ
- **WiFi AP**: WPA2, デフォルト SSID="esp-mp4player", Password="12345678" (常時ON)
- **設定ファイル**: `/sdcard/server.config` で SSID/パスワード/URL/初期ページをカスタマイズ可能
- **HTTP server**: `esp_http_server`、起動時に20個のURIハンドラ登録
- **プレイリスト**: `/sdcard/playlist/` フォルダ内の .mp4 ファイルを順次再生。直下にmp4がない場合はサブフォルダを選択
- **ハイブリッド起動**: WiFi起動 → playlistフォルダにmp4があれば自動再生 → ブラウザから操作可能
- **アップロード制限**: 再生中はアップロード不可（メモリ制約）
- **LCD表示**: Idle時はWiFi接続QRコード + 接続情報を表示、再生中は動画表示

### REST API
| Method | Path | 説明 |
|---|---|---|
| GET | `/` | リダイレクト（server.configのstart_pageへ302） |
| GET | `/player` | プレイヤーページ |
| GET | `/browse` | ファイルブラウザページ |
| GET | `/api/status` | `{playing, file, index, total, folder, playing_folder, sync_mode, volume}` |
| GET | `/api/playlist` | `{folder, files:[], folders:[]}` |
| POST | `/api/folder?name=xxx` | プレイリストフォルダ切替 |
| POST | `/api/play?file=xxx` or `?index=N` | 再生開始 |
| POST | `/api/stop` | 再生停止 |
| POST | `/api/next` / `/api/prev` | 次/前の動画 |
| POST | `/api/volume?vol=N` | 音量設定 (0–100) |
| POST | `/api/sync-mode?mode=audio\|video` | A/V同期モード切替 |
| POST | `/api/start-page?page=player\|browse` | 初期ページ設定（server.configに保存） |
| POST | `/api/save-player-config` | プレイヤー設定保存（volume, sync_mode, folderをplayer.configに） |
| GET | `/download?file=/path` | ファイルダウンロード |
| GET | `/preview?file=/path` | ファイルプレビュー |
| POST | `/upload?path=/&filename=xxx` | アップロード（Raw POST body） |
| POST | `/delete?file=/path` | 削除（再帰） |
| POST | `/rename?file=/path&name=new` | リネーム |
| POST | `/mkdir?path=/&name=dir` | フォルダ作成 |
| GET | `/storage-info` | `{total, used, free}` |

### server.config（SDカード設定ファイル）
`/sdcard/server.config` にテキストファイルを置くと WiFi AP/サーバー設定をカスタマイズできる。ファイルがなければデフォルト値を使用。
```
ssid=MyPlayer
password=secret123
url=192.168.4.1
start_page=player
```
| キー | デフォルト値 | 説明 |
|---|---|---|
| ssid | esp-mp4player | WiFi AP の SSID |
| password | 12345678 | WiFi AP のパスワード（空=オープン） |
| url | 192.168.4.1 | QR/LCD に表示する接続先URL |
| start_page | player | 初期ページ (`player` or `browse`) |

- `key=value` 形式。空行・`#` 始まりの行はスキップ
- 未指定のキーはデフォルト値を使用
- SD初期化後・WiFi起動前に読み込み（`load_server_config()`）

### player.config（プレイヤー設定ファイル）
`/sdcard/playlist/player.config` にテキストファイルを置くとプレイヤー設定を永続化できる。ファイルがなければデフォルト値を使用。
Web UIから設定を変更すると自動的にファイルに保存される。
```
volume=80
sync_mode=audio
folder=anime
```
| キー | デフォルト値 | 説明 |
|---|---|---|
| volume | 100 | 音量 (0–100) |
| sync_mode | audio | A/V同期モード (`audio` or `video`) |
| folder | (空) | プレイリストサブフォルダ名（空=ルート） |

- 音量: スライダーリリース時に自動保存
- Sync mode: トグル変更時に自動保存
- フォルダ: Web UI の「Set Default」ボタンで明示的に保存
- 起動時に `folder` が設定されていれば自動的にそのフォルダを選択して再生開始

### PSRAM 割り当てモード (重要)
全ボードで `CONFIG_SPIRAM_USE_MALLOC=y` を使用（`CAPS_ALLOC` ではない）。
`ALWAYSINTERNAL=4096` により 4KB超の `malloc()` は自動的にPSRAMを使用。
H.264デコーダのフレームバッファ（ソース解像度に比例）がPSRAMに確保され、
AAC デコーダ等の内部RAMが不足しない。
**注意:** `USE_MALLOC` と `USE_CAPS_ALLOC` は排他的choice。両方書くと後者が優先される。

### WiFi メモリ最適化 (sdkconfig)
```
CONFIG_ESP_WIFI_IRAM_OPT=n
CONFIG_ESP_WIFI_RX_IRAM_OPT=n
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=8
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=8
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5744
CONFIG_LWIP_TCP_WND_DEFAULT=5744
```

## Reference
- サンプルコード: `../Reference/ESP32-S3-LCD-1.3-Demo-New/Arduino/`
- ライブラリ: `../Reference/ESP32-S3-LCD-1.3-Demo-New/lib/`
- WiFiファイルサーバー: `https://github.com/bishi-eava/m5-wifi-fileserver`
