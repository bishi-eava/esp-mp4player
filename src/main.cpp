#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "board_config.h"
#include "lcd_config.h"
#include "mp4_player.h"

#ifdef BOARD_SD_MODE_SDMMC
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#endif

#ifdef BOARD_SD_MODE_SPI
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#endif

static const char *TAG = "main";

#define MP4_FILE_PATH "/sdcard/video.mp4"

// --- Display ---
static LGFX display;

// --- SD Card init ---
static bool init_sdcard(void)
{
#ifdef BOARD_SD_MODE_SDMMC
    ESP_LOGI(TAG, "Initializing SD card (SDMMC 1-bit mode)");

    gpio_set_direction((gpio_num_t)BOARD_SD_D3, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)BOARD_SD_D3, 1);

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = (gpio_num_t)BOARD_SD_CLK;
    slot_config.cmd = (gpio_num_t)BOARD_SD_CMD;
    slot_config.d0  = (gpio_num_t)BOARD_SD_D0;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
#endif

#ifdef BOARD_SD_MODE_SPI
    ESP_LOGI(TAG, "Initializing SD card (SPI mode)");

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = BOARD_SD_SPI_MOSI;
    bus_cfg.miso_io_num = BOARD_SD_SPI_MISO;
    bus_cfg.sclk_io_num = BOARD_SD_SPI_CLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;

    esp_err_t ret = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BOARD_SD_SPI_CS;
    slot_config.host_id = BOARD_SD_SPI_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_card_t *card;
    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
#endif

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "SD card mounted successfully");
    sdmmc_card_print_info(stdout, card);
    return true;
}

// --- Display init ---
static void init_display(void)
{
    ESP_LOGI(TAG, "Initializing display");
    display.init();
    display.setRotation(BOARD_DISPLAY_ROTATION);
    display.setSwapBytes(true);
    display.setBrightness(255);
    display.fillScreen(TFT_BLACK);
    ESP_LOGI(TAG, "Display initialized: %dx%d", display.width(), display.height());
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 MP4 Movie Player starting...");

    // Phase 1: SD card first (SPI bus must be initialized before display to avoid conflict)
    bool sd_ok = init_sdcard();

    // Phase 2: Display
    init_display();
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1);
    display.setCursor(10, 10);
    display.println("MP4 Player");
    display.printf("SD: %s\n", sd_ok ? "OK" : "FAILED");

    if (!sd_ok) {
        display.setTextColor(TFT_RED, TFT_BLACK);
        display.println("Insert SD & reboot");
        return;
    }

    // Phase 3: Create queue and playback tasks
    display.println("Playing video...");
    vTaskDelay(pdMS_TO_TICKS(500));

    static player_ctx_t ctx = {};
    ctx.filepath = MP4_FILE_PATH;
    ctx.display = &display;
    ctx.video_queue = xQueueCreate(4, sizeof(frame_msg_t));

    // video_task first (higher priority, waits on queue)
    // demux_task second (lower priority, feeds the queue)
    xTaskCreatePinnedToCore(video_task, "video", 48 * 1024, &ctx, 5, nullptr, 0);
    xTaskCreatePinnedToCore(demux_task, "demux",  8 * 1024, &ctx, 4, nullptr, 0);
}
