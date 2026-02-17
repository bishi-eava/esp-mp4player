#pragma once

#include <cstring>
#include "esp_http_server.h"
#include "lcd_config.h"
#include "media_controller.h"

namespace mp4 {

// WiFi AP + HTTP server configuration, loadable from /sdcard/server.config
struct ServerConfig {
    char ssid[33];
    char password[65];
    char url[64];
    char start_page[16];

    ServerConfig() {
        strlcpy(ssid, "esp-mp4player", sizeof(ssid));
        strlcpy(password, "12345678", sizeof(password));
        strlcpy(url, "192.168.4.1", sizeof(url));
        strlcpy(start_page, "player", sizeof(start_page));
    }
};

// Load server config from a key=value text file.
// Returns default values for any missing keys or if the file doesn't exist.
ServerConfig load_server_config(const char *path);
void save_server_config(const char *path, const ServerConfig &cfg);

class FileServer {
public:
    FileServer(LGFX &display, MediaController &controller, const ServerConfig &config)
        : display_(display), controller_(controller), config_(config) {}

    // Initialize WiFi AP + start HTTP server. Non-blocking.
    void start();

private:
    void init_wifi_ap();
    void start_http_server();
    void show_connection_info();

    // Page handlers
    static esp_err_t index_redirect_handler(httpd_req_t *req);
    static esp_err_t player_handler(httpd_req_t *req);
    static esp_err_t browse_handler(httpd_req_t *req);

    // Player control handlers
    static esp_err_t status_handler(httpd_req_t *req);
    static esp_err_t playlist_handler(httpd_req_t *req);
    static esp_err_t folder_handler(httpd_req_t *req);
    static esp_err_t play_handler(httpd_req_t *req);
    static esp_err_t stop_handler(httpd_req_t *req);
    static esp_err_t next_handler(httpd_req_t *req);
    static esp_err_t prev_handler(httpd_req_t *req);
    static esp_err_t sync_mode_handler(httpd_req_t *req);
    static esp_err_t volume_handler(httpd_req_t *req);
    static esp_err_t start_page_handler(httpd_req_t *req);

    // File management handlers
    static esp_err_t download_handler(httpd_req_t *req);
    static esp_err_t preview_handler(httpd_req_t *req);
    static esp_err_t upload_handler(httpd_req_t *req);
    static esp_err_t delete_handler(httpd_req_t *req);
    static esp_err_t rename_handler(httpd_req_t *req);
    static esp_err_t mkdir_handler(httpd_req_t *req);
    static esp_err_t storage_handler(httpd_req_t *req);

    // Utility
    static const char *get_content_type(const char *filepath);

    LGFX &display_;
    MediaController &controller_;
    ServerConfig config_;
    httpd_handle_t server_ = nullptr;
};

}  // namespace mp4
