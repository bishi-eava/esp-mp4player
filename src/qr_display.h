#pragma once

// QR code generation and LovyanGFX display rendering
// Uses espressif/qrcode component (callback-based API)

#include "qrcode.h"
#include <LovyanGFX.hpp>

namespace mp4 {

namespace detail {

struct QrDrawCtx {
    LGFX *display;
    int cx, cy;       // center of QR code area
    int max_size;     // max QR code size in pixels (width = height)
};

inline void qr_draw_callback(esp_qrcode_handle_t qrcode, void *arg)
{
    auto *ctx = static_cast<QrDrawCtx *>(arg);
    int modules = esp_qrcode_get_size(qrcode);

    // Calculate pixel size to fit within max_size (including 2-module quiet zone per side)
    int px = ctx->max_size / (modules + 4);
    if (px < 1) px = 1;

    int qr_px = modules * px;
    int x0 = ctx->cx - qr_px / 2;
    int y0 = ctx->cy - qr_px / 2;

    // White background with quiet zone
    int quiet = px * 2;
    ctx->display->fillRect(x0 - quiet, y0 - quiet,
                           qr_px + quiet * 2, qr_px + quiet * 2, TFT_WHITE);

    // Draw black modules
    for (int y = 0; y < modules; y++) {
        for (int x = 0; x < modules; x++) {
            if (esp_qrcode_get_module(qrcode, x, y)) {
                ctx->display->fillRect(x0 + x * px, y0 + y * px, px, px, TFT_BLACK);
            }
        }
    }
}

}  // namespace detail

// Draw a QR code centered at (cx, cy) within a max_size bounding box.
// The QR code is rendered with white background + quiet zone + black modules.
inline void draw_qrcode(LGFX &display, const char *text, int cx, int cy, int max_size)
{
    detail::QrDrawCtx ctx = { &display, cx, cy, max_size };

    esp_qrcode_config_t cfg = {};
    cfg.display_func_with_cb = detail::qr_draw_callback;
    cfg.max_qrcode_version = 10;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    cfg.user_data = &ctx;

    esp_qrcode_generate(&cfg, text);
}

// Show WiFi connection info with QR code on the display.
// Layout: QR code (upper ~75%), text info (lower ~25%)
inline void show_wifi_qr(LGFX &display, const char *ssid, const char *password, const char *url)
{
    int w = display.width();
    int h = display.height();

    display.fillScreen(TFT_BLACK);

    // Build WiFi QR code string: WIFI:T:WPA;S:<ssid>;P:<password>;;
    char wifi_qr[128];
    if (password && strlen(password) > 0) {
        snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
    } else {
        snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:nopass;S:%s;;", ssid);
    }

    // QR code takes upper 75% of display, text takes lower 25%
    int qr_area_h = h * 3 / 4;
    int qr_size = (qr_area_h < w) ? qr_area_h : w;  // min(qr_area_h, w)
    qr_size -= 4;  // small margin

    draw_qrcode(display, wifi_qr, w / 2, qr_area_h / 2, qr_size);

    // Text below QR code
    int text_y = qr_area_h + 2;
    int font_h = 10;  // approximate height per line at textsize 1

    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.setTextSize(1);

    // Center text: SSID
    char line[64];
    snprintf(line, sizeof(line), "WiFi: %s", ssid);
    int tw = display.textWidth(line);
    display.setCursor((w - tw) / 2, text_y);
    display.print(line);

    // URL
    text_y += font_h;
    tw = display.textWidth(url);
    display.setCursor((w - tw) / 2, text_y);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.print(url);

    // Password (if space allows)
    text_y += font_h;
    if (text_y + font_h <= h) {
        snprintf(line, sizeof(line), "Pass: %s", password);
        tw = display.textWidth(line);
        display.setCursor((w - tw) / 2, text_y);
        display.setTextColor(0x7BEF, TFT_BLACK);  // gray
        display.print(line);
    }
}

}  // namespace mp4
