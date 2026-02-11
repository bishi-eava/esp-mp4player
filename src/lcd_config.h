#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// SpotPear ESP32-S3 LCD 1.3inch (ST7789 240x240) configuration
class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Panel_ST7789  _panel_instance;
    lgfx::Light_PWM     _light_instance;

public:
    LGFX(void) {
        // SPI bus
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 40;
            cfg.pin_mosi = 41;
            cfg.pin_miso = -1;
            cfg.pin_dc   = 38;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // ST7789 panel
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs   = 39;
            cfg.pin_rst  = 42;
            cfg.pin_busy = -1;
            cfg.panel_width  = 240;
            cfg.panel_height = 240;
            cfg.offset_x     = 0;
            cfg.offset_y     = 0;
            cfg.offset_rotation = 0;
            cfg.invert   = true;   // IPS panel
            cfg.rgb_order = false;  // BGR order (ST7789 default)
            _panel_instance.config(cfg);
        }

        // Backlight (GPIO20, PWM)
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = 20;
            cfg.invert = false;
            cfg.freq   = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};
