#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board_config.h"

#if defined(BOARD_ATOMS3R)
// I2C LED driver backlight for Atom S3R (device at 0x30)
// Must init I2C LED driver before brightness control works.
// Based on M5GFX Light_M5StackAtomS3R implementation.
struct Light_AtomS3R : public lgfx::ILight {
    bool init(uint8_t brightness) override {
        lgfx::i2c::init(BOARD_BL_I2C_PORT, BOARD_BL_I2C_SDA, BOARD_BL_I2C_SCL);
        lgfx::i2c::writeRegister8(BOARD_BL_I2C_PORT, BOARD_BL_I2C_ADDR, 0x00, 0b01000000, 0, BOARD_BL_I2C_FREQ);
        lgfx::delay(1);
        lgfx::i2c::writeRegister8(BOARD_BL_I2C_PORT, BOARD_BL_I2C_ADDR, 0x08, 0b00000001, 0, BOARD_BL_I2C_FREQ);
        lgfx::i2c::writeRegister8(BOARD_BL_I2C_PORT, BOARD_BL_I2C_ADDR, 0x70, 0b00000000, 0, BOARD_BL_I2C_FREQ);
        setBrightness(brightness);
        return true;
    }

    void setBrightness(uint8_t brightness) override {
        lgfx::i2c::writeRegister8(BOARD_BL_I2C_PORT, BOARD_BL_I2C_ADDR, 0x0e, brightness, 0, BOARD_BL_I2C_FREQ);
    }
};
#endif

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI       _bus_instance;
#if defined(BOARD_SPOTPEAR)
    lgfx::Panel_ST7789  _panel_instance;
    lgfx::Light_PWM     _light_instance;
#elif defined(BOARD_ATOMS3R)
    lgfx::Panel_GC9107  _panel_instance;
    Light_AtomS3R       _light_instance;
#endif

public:
    LGFX(void) {
        // SPI bus
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host   = BOARD_LCD_SPI_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = BOARD_LCD_SPI_FREQ;
            cfg.freq_read  = 16000000;
#if defined(BOARD_ATOMS3R)
            cfg.spi_3wire  = true;   // GC9107 uses 3-wire SPI (bidirectional MOSI)
#endif
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = BOARD_LCD_SCK;
            cfg.pin_mosi = BOARD_LCD_MOSI;
            cfg.pin_miso = -1;
            cfg.pin_dc   = BOARD_LCD_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // Panel
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs   = BOARD_LCD_CS;
            cfg.pin_rst  = BOARD_LCD_RST;
            cfg.pin_busy = -1;
            cfg.panel_width  = BOARD_DISPLAY_WIDTH;
            cfg.panel_height = BOARD_DISPLAY_HEIGHT;
            cfg.offset_x     = BOARD_LCD_OFFSET_X;
            cfg.offset_y     = BOARD_LCD_OFFSET_Y;
            cfg.offset_rotation = 0;
            cfg.invert   = BOARD_LCD_INVERT;
            cfg.rgb_order = false;
            _panel_instance.config(cfg);
        }

        // Backlight
        {
#if defined(BOARD_SPOTPEAR)
            auto cfg = _light_instance.config();
            cfg.pin_bl = BOARD_LCD_BL;
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
#endif
            // ATOMS3R: Light_AtomS3R uses I2C, no config needed here
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};
