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
// Pin mapping differs from AtomS3 (non-R): SCLK=15, DC=42, CS=14, RST=48
#define BOARD_DISPLAY_WIDTH    128
#define BOARD_DISPLAY_HEIGHT   128
#define BOARD_DISPLAY_ROTATION 0
#define BOARD_LCD_SPI_HOST     SPI3_HOST
#define BOARD_LCD_SCK          15
#define BOARD_LCD_MOSI         21
#define BOARD_LCD_DC           42
#define BOARD_LCD_CS           14
#define BOARD_LCD_RST          48
#define BOARD_LCD_BL           16   // PWM backlight (also needs I2C LED driver init)
#define BOARD_LCD_OFFSET_X     0
#define BOARD_LCD_OFFSET_Y     32   // GC9107 memory is 128x160, display is 128x128, starts at row 32
#define BOARD_LCD_INVERT       false
#define BOARD_LCD_SPI_FREQ     40000000

// I2C LED driver for backlight (address 0x30, init required before PWM works)
#define BOARD_BL_I2C_PORT      I2C_NUM_1
#define BOARD_BL_I2C_SDA       45
#define BOARD_BL_I2C_SCL       0
#define BOARD_BL_I2C_ADDR      0x30
#define BOARD_BL_I2C_FREQ      400000

// SD Card: SPI mode via ATOMIC TF Card Reader (SPI2_HOST)
#define BOARD_SD_MODE_SPI
#define BOARD_SD_SPI_HOST      SPI2_HOST
#define BOARD_SD_SPI_MOSI      6
#define BOARD_SD_SPI_MISO      8
#define BOARD_SD_SPI_CLK       7
#define BOARD_SD_SPI_CS        GPIO_NUM_4

#elif defined(BOARD_ATOMS3R_SPK)
// ============================================================
// M5Stack Atom S3R + SPK Base (GC9107 128x128, NS4168 I2S DAC)
// ============================================================

// Display (identical to ATOMS3R)
#define BOARD_DISPLAY_WIDTH    128
#define BOARD_DISPLAY_HEIGHT   128
#define BOARD_DISPLAY_ROTATION 0
#define BOARD_LCD_SPI_HOST     SPI3_HOST
#define BOARD_LCD_SCK          15
#define BOARD_LCD_MOSI         21
#define BOARD_LCD_DC           42
#define BOARD_LCD_CS           14
#define BOARD_LCD_RST          48
#define BOARD_LCD_BL           16
#define BOARD_LCD_OFFSET_X     0
#define BOARD_LCD_OFFSET_Y     32
#define BOARD_LCD_INVERT       false
#define BOARD_LCD_SPI_FREQ     40000000

// I2C LED driver for backlight (identical to ATOMS3R)
#define BOARD_BL_I2C_PORT      I2C_NUM_1
#define BOARD_BL_I2C_SDA       45
#define BOARD_BL_I2C_SCL       0
#define BOARD_BL_I2C_ADDR      0x30
#define BOARD_BL_I2C_FREQ      400000

// SD Card: SPI mode via SPK Base TF slot (SPI2_HOST)
// CS is hardwired on PCB → GPIO_NUM_NC (-1)
#define BOARD_SD_MODE_SPI
#define BOARD_SD_SPI_HOST      SPI2_HOST
#define BOARD_SD_SPI_MOSI      6
#define BOARD_SD_SPI_MISO      8
#define BOARD_SD_SPI_CLK       7
#define BOARD_SD_SPI_CS        GPIO_NUM_NC

// I2S Audio output (NS4168 DAC on SPK Base)
#define BOARD_I2S_BCLK         GPIO_NUM_5
#define BOARD_I2S_LRCLK        GPIO_NUM_39
#define BOARD_I2S_DOUT         GPIO_NUM_38

#else
#error "No board defined! Use -DBOARD_SPOTPEAR, -DBOARD_ATOMS3R, or -DBOARD_ATOMS3R_SPK"
#endif

// Max decode resolution (half of Full HD, common to all boards)
#define BOARD_MAX_DECODE_WIDTH   960
#define BOARD_MAX_DECODE_HEIGHT  540
