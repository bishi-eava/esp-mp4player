#pragma once

#include "driver/gpio.h"

// Board-specific configuration for ESP32-S3 MP4 Player
// Select board via build_flags: -DBOARD_SPOTPEAR or -DBOARD_ATOMS3R

#if defined(BOARD_SPOTPEAR)
// ============================================================
// SpotPear ESP32-S3 LCD 1.3inch (ST7789 240x240)
// ============================================================

// Display
#define BOARD_DISPLAY_WIDTH    240
#define BOARD_DISPLAY_HEIGHT   240
#define BOARD_DISPLAY_ROTATION 0
#define BOARD_LCD_SPI_HOST     SPI2_HOST
#define BOARD_LCD_SCK          40
#define BOARD_LCD_MOSI         41
#define BOARD_LCD_DC           38
#define BOARD_LCD_CS           39
#define BOARD_LCD_RST          42
#define BOARD_LCD_BL           20
#define BOARD_LCD_OFFSET_X     0
#define BOARD_LCD_OFFSET_Y     0
#define BOARD_LCD_INVERT       true
#define BOARD_LCD_SPI_FREQ     40000000

// SD Card: SDMMC 1-bit mode
#define BOARD_SD_MODE_SDMMC
#define BOARD_SD_D0            16
#define BOARD_SD_D3            17
#define BOARD_SD_CMD           18
#define BOARD_SD_CLK           21

#elif defined(BOARD_ATOMS3R)
// ============================================================
// M5Stack Atom S3R (GC9107 128x128) + ATOMIC TF Card Reader
// ============================================================

// Display (SPI3_HOST to avoid conflict with SD on SPI2_HOST)
#define BOARD_DISPLAY_WIDTH    128
#define BOARD_DISPLAY_HEIGHT   128
#define BOARD_DISPLAY_ROTATION 0
#define BOARD_LCD_SPI_HOST     SPI3_HOST
#define BOARD_LCD_SCK          17
#define BOARD_LCD_MOSI         21
#define BOARD_LCD_DC           33
#define BOARD_LCD_CS           15
#define BOARD_LCD_RST          34
#define BOARD_LCD_BL           16
#define BOARD_LCD_OFFSET_X     2
#define BOARD_LCD_OFFSET_Y     1
#define BOARD_LCD_INVERT       true
#define BOARD_LCD_SPI_FREQ     40000000

// SD Card: SPI mode via ATOMIC TF Card Reader (SPI2_HOST)
#define BOARD_SD_MODE_SPI
#define BOARD_SD_SPI_HOST      SPI2_HOST
#define BOARD_SD_SPI_MOSI      6
#define BOARD_SD_SPI_MISO      8
#define BOARD_SD_SPI_CLK       7
#define BOARD_SD_SPI_CS        GPIO_NUM_4

#else
#error "No board defined! Use -DBOARD_SPOTPEAR or -DBOARD_ATOMS3R"
#endif
