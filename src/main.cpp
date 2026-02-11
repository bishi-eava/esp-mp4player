#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

#include "lcd_config.h"
#include "mp4_player.h"

static const char *TAG = "main";

// --- Pin definitions ---
#define SDMMC_D0  16
#define SDMMC_D3  17
#define SDMMC_CMD 18
#define SDMMC_CLK 21

#define MP4_FILE_PATH "/sdcard/video.mp4"

// --- Display ---
static LGFX display;

// --- SD Card init ---
static bool init_sdcard(void)
{
    ESP_LOGI(TAG, "Initializing SD card (SDMMC 1-bit mode)");

    gpio_set_direction((gpio_num_t)SDMMC_D3, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)SDMMC_D3, 1);

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = (gpio_num_t)SDMMC_CLK;
    slot_config.cmd = (gpio_num_t)SDMMC_CMD;
    slot_config.d0  = (gpio_num_t)SDMMC_D0;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
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
    display.setRotation(0);
    display.setSwapBytes(true);
    display.setBrightness(255);
    display.fillScreen(TFT_BLACK);
    ESP_LOGI(TAG, "Display initialized: %dx%d", display.width(), display.height());
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 MP4 Movie Player starting...");

    init_display();

    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1);
    display.setCursor(10, 10);
    display.println("MP4 Player");
    display.println("Init SD card...");

    if (!init_sdcard()) {
        display.setTextColor(TFT_RED, TFT_BLACK);
        display.println("SD card failed!");
        return;
    }
    display.println("SD card OK");

    // Play video in a dedicated task with large stack (H.264 decoder needs ~32KB+)
    display.println("Playing video...");
    vTaskDelay(pdMS_TO_TICKS(500));

    static LGFX *disp_ptr = &display;
    xTaskCreatePinnedToCore(
        [](void *arg) {
            LGFX *d = (LGFX *)arg;
            play_mp4(*d, MP4_FILE_PATH);

            d->fillScreen(TFT_BLACK);
            d->setCursor(10, 100);
            d->setTextColor(TFT_GREEN, TFT_BLACK);
            d->println("Playback finished");
            ESP_LOGI("main", "Done");
            vTaskDelete(nullptr);
        },
        "playback",
        64 * 1024,
        disp_ptr,
        5,
        nullptr,
        0
    );
}
