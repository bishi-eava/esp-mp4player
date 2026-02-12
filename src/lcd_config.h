#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board_config.h"

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI       _bus_instance;
#if defined(BOARD_SPOTPEAR)
    lgfx::Panel_ST7789  _panel_instance;
#elif defined(BOARD_ATOMS3R)
    lgfx::Panel_GC9107  _panel_instance;
#endif
    lgfx::Light_PWM     _light_instance;

public:
    LGFX(void) {
        // SPI bus
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host   = BOARD_LCD_SPI_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = BOARD_LCD_SPI_FREQ;
            cfg.freq_read  = 16000000;
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

        // Backlight (PWM)
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = BOARD_LCD_BL;
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};
