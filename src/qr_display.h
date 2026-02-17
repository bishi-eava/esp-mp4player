#pragma once

// QR code generation and LovyanGFX display rendering
// Uses qrcodegen library directly for min_version control

#include <cstdint>
#include <LovyanGFX.hpp>

// Forward declarations from qrcodegen (private header of espressif/qrcode component).
// The symbols are compiled and linked via the component; we only need declarations.
extern "C" {
#define qrcodegen_BUFFER_LEN_FOR_VERSION(n)  ((((n) * 4 + 17) * ((n) * 4 + 17) + 7) / 8 + 1)
enum qrcodegen_Ecc { qrcodegen_Ecc_LOW = 0, qrcodegen_Ecc_MEDIUM, qrcodegen_Ecc_QUARTILE, qrcodegen_Ecc_HIGH };
enum qrcodegen_Mask { qrcodegen_Mask_AUTO = -1 };
bool qrcodegen_encodeText(const char *text, uint8_t tempBuffer[], uint8_t qrcode[],
    enum qrcodegen_Ecc ecl, int minVersion, int maxVersion, enum qrcodegen_Mask mask, bool boostEcl);
int qrcodegen_getSize(const uint8_t qrcode[]);
bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);
}

namespace mp4 {

// Background colors for QR cycle screens (RGB565)
constexpr uint16_t kBgDarkGreen  = 0x0180;  // ~RGB(0, 48, 0)
constexpr uint16_t kBgDarkOrange = 0x5100;  // ~RGB(80, 32, 0)

constexpr int kQrMaxVersion = 10;

// Draw a QR code centered at (cx, cy) within a max_size bounding box.
// min_version forces a minimum QR version to ensure consistent display size.
inline void draw_qrcode(LGFX &display, const char *text, int cx, int cy, int max_size,
                         int min_version = 1)
{
    uint8_t tempbuf[qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion)];
    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(kQrMaxVersion)];

    bool ok = qrcodegen_encodeText(text, tempbuf, qrcode,
                                    qrcodegen_Ecc_LOW, min_version, kQrMaxVersion,
                                    qrcodegen_Mask_AUTO, true);
    if (!ok) return;

    int modules = qrcodegen_getSize(qrcode);

    // Calculate pixel size to fit within max_size (including 1-module quiet zone per side)
    int px = max_size / (modules + 2);
    if (px < 1) px = 1;

    int qr_px = modules * px;
    int x0 = cx - qr_px / 2;
    int y0 = cy - qr_px / 2;

    // White background with quiet zone
    int quiet = px;
    display.fillRect(x0 - quiet, y0 - quiet,
                     qr_px + quiet * 2, qr_px + quiet * 2, TFT_WHITE);

    // Draw black modules
    for (int y = 0; y < modules; y++) {
        for (int x = 0; x < modules; x++) {
            if (qrcodegen_getModule(qrcode, x, y)) {
                display.fillRect(x0 + x * px, y0 + y * px, px, px, TFT_BLACK);
            }
        }
    }
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

    // QR code fills display minus text area at bottom
    int font_h = 10;
    int text_area = font_h * 2 + 4;
    int qr_area_h = h - text_area;
    int qr_size = (qr_area_h < w) ? qr_area_h : w;

    draw_qrcode(display, wifi_qr, w / 2, qr_area_h / 2, qr_size);

    // Text below QR code
    int text_y = qr_area_h + 2;

    display.setTextSize(1);
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    char line[64];
    snprintf(line, sizeof(line), "WiFi: %s", ssid);
    int tw = display.textWidth(line);
    display.setCursor((w - tw) / 2, text_y);
    display.print(line);

    text_y += font_h;
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    tw = display.textWidth(url);
    display.setCursor((w - tw) / 2, text_y);
    display.print(url);
}

// --- QR Cycle Screens (3 screens, rotated by caller) ---

// Screen 0: WiFi auto-connect QR
inline void show_wifi_connect_qr(LGFX &display, const char *ssid, const char *password)
{
    int w = display.width();
    int h = display.height();

    display.fillScreen(kBgDarkGreen);

    char wifi_qr[128];
    if (password && strlen(password) > 0) {
        snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
    } else {
        snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:nopass;S:%s;;", ssid);
    }

    int font_h = 10;
    int text_area = font_h * 2 + 4;
    int qr_area_h = h - text_area;
    int qr_size = (qr_area_h < w) ? qr_area_h : w;

    draw_qrcode(display, wifi_qr, w / 2, qr_area_h / 2, qr_size);

    int text_y = qr_area_h + 2;

    display.setTextSize(1);
    display.setTextColor(TFT_GREEN, kBgDarkGreen);
    const char *label = "Auto AP Connect";
    int tw = display.textWidth(label);
    display.setCursor((w - tw) / 2, text_y);
    display.print(label);

    text_y += font_h;
    display.setTextColor(TFT_CYAN, kBgDarkGreen);
    tw = display.textWidth(ssid);
    display.setCursor((w - tw) / 2, text_y);
    display.print(ssid);
}

// Screen 1: URL QR
inline void show_url_qr(LGFX &display, const char *url)
{
    int w = display.width();
    int h = display.height();

    display.fillScreen(kBgDarkOrange);

    char url_str[128];
    snprintf(url_str, sizeof(url_str), "http://%s/", url);

    int font_h = 10;
    int text_area = font_h * 2 + 4;
    int qr_area_h = h - text_area;
    int qr_size = (qr_area_h < w) ? qr_area_h : w;

    draw_qrcode(display, url_str, w / 2, qr_area_h / 2, qr_size, 3);

    int text_y = qr_area_h + 2;

    display.setTextSize(1);
    display.setTextColor(TFT_WHITE, kBgDarkOrange);
    const char *label = "App Page";
    int tw = display.textWidth(label);
    display.setCursor((w - tw) / 2, text_y);
    display.print(label);

    text_y += font_h;
    display.setTextColor(TFT_CYAN, kBgDarkOrange);
    tw = display.textWidth(url_str);
    display.setCursor((w - tw) / 2, text_y);
    display.print(url_str);
}

// Screen 2: Text display (SSID, Password, URL)
inline void show_connection_text(LGFX &display, const char *ssid, const char *password, const char *url)
{
    int w = display.width();
    int h = display.height();

    display.fillScreen(TFT_BLACK);
    display.setTextSize(1);

    int font_h = 10;
    int total_h = font_h * 7;  // 3 labels + 3 values + spacing
    int y = (h - total_h) / 2;

    // SSID
    display.setTextColor(0x7BEF, TFT_BLACK);  // gray
    const char *ssid_label = "SSID";
    int tw = display.textWidth(ssid_label);
    display.setCursor((w - tw) / 2, y);
    display.print(ssid_label);
    y += font_h;

    display.setTextColor(TFT_WHITE, TFT_BLACK);
    tw = display.textWidth(ssid);
    display.setCursor((w - tw) / 2, y);
    display.print(ssid);
    y += font_h + font_h / 2;

    // Password
    display.setTextColor(0x7BEF, TFT_BLACK);
    const char *pass_label = "Password";
    tw = display.textWidth(pass_label);
    display.setCursor((w - tw) / 2, y);
    display.print(pass_label);
    y += font_h;

    display.setTextColor(TFT_WHITE, TFT_BLACK);
    tw = display.textWidth(password);
    display.setCursor((w - tw) / 2, y);
    display.print(password);
    y += font_h + font_h / 2;

    // URL
    display.setTextColor(0x7BEF, TFT_BLACK);
    const char *url_label = "URL";
    tw = display.textWidth(url_label);
    display.setCursor((w - tw) / 2, y);
    display.print(url_label);
    y += font_h;

    char url_str[128];
    snprintf(url_str, sizeof(url_str), "http://%s/", url);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    tw = display.textWidth(url_str);
    display.setCursor((w - tw) / 2, y);
    display.print(url_str);
}

// Unified cycle screen dispatcher
inline void show_qr_cycle_screen(LGFX &display, int screen,
                                  const char *ssid, const char *password, const char *url)
{
    switch (screen % 3) {
    case 0: show_wifi_connect_qr(display, ssid, password); break;
    case 1: show_url_qr(display, url); break;
    case 2: show_connection_text(display, ssid, password, url); break;
    }
}

}  // namespace mp4
