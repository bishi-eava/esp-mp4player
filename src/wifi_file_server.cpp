#include "wifi_file_server.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <cerrno>
#include <sys/time.h>
#include <time.h>

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"

#include "player_constants.h"
#include "html_content.h"
#include "qr_display.h"

// snprintf truncation is acceptable for SD card paths (naturally bounded by FAT FS)
#pragma GCC diagnostic ignored "-Wformat-truncation"

static const char *TAG = "fileserver";

namespace mp4 {

// ---- Utility functions ----

// URL percent-decode in place (decoded string is always <= encoded string length)
static void url_decode_in_place(char *str)
{
    char *dst = str;
    const char *src = str;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            *dst++ = (char)strtol(hex, nullptr, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Extract and URL-decode a query parameter value
static esp_err_t get_decoded_query_param(const char *query, const char *key, char *val, size_t val_size)
{
    esp_err_t err = httpd_query_key_value(query, key, val, val_size);
    if (err == ESP_OK) {
        url_decode_in_place(val);
    }
    return err;
}

static void format_size(size_t bytes, char *buf, size_t buf_size)
{
    if (bytes < 1024) {
        snprintf(buf, buf_size, "%u Byte", (unsigned)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, buf_size, "%u KB", (unsigned)(bytes / 1024));
    } else {
        snprintf(buf, buf_size, "%u MB", (unsigned)(bytes / (1024 * 1024)));
    }
}

static void format_datetime(time_t t, char *buf, size_t buf_size)
{
    if (t == 0) {
        snprintf(buf, buf_size, "-");
        return;
    }
    struct tm *tm = localtime(&t);
    snprintf(buf, buf_size, "%04d/%02d/%02d %02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min);
}

static void sync_time_from_query(httpd_req_t *req, const char *query)
{
    char time_param[20] = "";
    if (get_decoded_query_param(query, "time", time_param, sizeof(time_param)) == ESP_OK) {
        time_t t = (time_t)atol(time_param);
        if (t > 0) {
            struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
        }
    }

    // Set timezone from client's getTimezoneOffset() (e.g. -540 for JST)
    // JS and POSIX TZ use same sign convention: JST=-540min → "UTC-9"
    char tz_param[10] = "";
    if (get_decoded_query_param(query, "tz", tz_param, sizeof(tz_param)) == ESP_OK) {
        int offset_min = atoi(tz_param);
        int hours = offset_min / 60;
        int mins = abs(offset_min) % 60;
        char tz_str[16];
        if (mins == 0) {
            snprintf(tz_str, sizeof(tz_str), "UTC%+d", hours);
        } else {
            snprintf(tz_str, sizeof(tz_str), "UTC%+d:%02d", hours, mins);
        }
        setenv("TZ", tz_str, 1);
        tzset();
    }
}

// ---- Server config loader ----

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) *end-- = '\0';
    return s;
}

ServerConfig load_server_config(const char *path)
{
    ServerConfig cfg;

    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGI(TAG, "No config file at %s, using defaults (SSID=%s)", path, cfg.ssid);
        return cfg;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(p);
        char *val = trim(eq + 1);

        if (strcmp(key, "ssid") == 0) {
            strlcpy(cfg.ssid, val, sizeof(cfg.ssid));
        } else if (strcmp(key, "password") == 0) {
            strlcpy(cfg.password, val, sizeof(cfg.password));
        } else if (strcmp(key, "url") == 0) {
            strlcpy(cfg.url, val, sizeof(cfg.url));
        } else if (strcmp(key, "start_page") == 0) {
            strlcpy(cfg.start_page, val, sizeof(cfg.start_page));
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "Loaded config: SSID=%s, URL=%s, start_page=%s", cfg.ssid, cfg.url, cfg.start_page);
    return cfg;
}

void save_server_config(const char *path, const ServerConfig &cfg)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to write config to %s", path);
        return;
    }
    fprintf(f, "ssid=%s\npassword=%s\nurl=%s\nstart_page=%s\n",
            cfg.ssid, cfg.password, cfg.url, cfg.start_page);
    fclose(f);
    ESP_LOGI(TAG, "Saved config to %s", path);
}

// ---- WiFi AP initialization ----

void FileServer::init_wifi_ap()
{
    // NVS is required by esp_wifi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    strlcpy((char *)wifi_config.ap.ssid, config_.ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(config_.ssid);
    wifi_config.ap.channel = kApChannel;
    wifi_config.ap.max_connection = kApMaxConnections;

    if (strlen(config_.password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strlcpy((char *)wifi_config.ap.password, config_.password, sizeof(wifi_config.ap.password));
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s, Channel=%d", config_.ssid, kApChannel);
}

// ---- HTTP server setup ----

void FileServer::start_http_server()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = kHttpServerStack;
    config.max_uri_handlers = kHttpMaxUriHandlers;
    config.max_open_sockets = 3;

    ESP_ERROR_CHECK(httpd_start(&server_, &config));

    // Page handlers
    httpd_uri_t index_uri  = { .uri = "/",        .method = HTTP_GET, .handler = index_redirect_handler, .user_ctx = this };
    httpd_uri_t player_uri = { .uri = "/player",   .method = HTTP_GET, .handler = player_handler,         .user_ctx = this };
    httpd_uri_t browse_uri = { .uri = "/browse",   .method = HTTP_GET, .handler = browse_handler,         .user_ctx = this };
    httpd_register_uri_handler(server_, &index_uri);
    httpd_register_uri_handler(server_, &player_uri);
    httpd_register_uri_handler(server_, &browse_uri);

    // Player control API
    httpd_uri_t status_uri   = { .uri = "/api/status",   .method = HTTP_GET,  .handler = status_handler,   .user_ctx = this };
    httpd_uri_t playlist_uri = { .uri = "/api/playlist", .method = HTTP_GET,  .handler = playlist_handler, .user_ctx = this };
    httpd_uri_t folder_uri   = { .uri = "/api/folder",   .method = HTTP_POST, .handler = folder_handler,   .user_ctx = this };
    httpd_uri_t play_uri     = { .uri = "/api/play",     .method = HTTP_POST, .handler = play_handler,     .user_ctx = this };
    httpd_uri_t stop_uri     = { .uri = "/api/stop",     .method = HTTP_POST, .handler = stop_handler,     .user_ctx = this };
    httpd_uri_t next_uri     = { .uri = "/api/next",     .method = HTTP_POST, .handler = next_handler,     .user_ctx = this };
    httpd_uri_t prev_uri     = { .uri = "/api/prev",     .method = HTTP_POST, .handler = prev_handler,     .user_ctx = this };
    httpd_uri_t syncmode_uri = { .uri = "/api/sync-mode", .method = HTTP_POST, .handler = sync_mode_handler, .user_ctx = this };
    httpd_uri_t volume_uri   = { .uri = "/api/volume",    .method = HTTP_POST, .handler = volume_handler,    .user_ctx = this };

    httpd_register_uri_handler(server_, &status_uri);
    httpd_register_uri_handler(server_, &playlist_uri);
    httpd_register_uri_handler(server_, &folder_uri);
    httpd_register_uri_handler(server_, &play_uri);
    httpd_register_uri_handler(server_, &stop_uri);
    httpd_register_uri_handler(server_, &next_uri);
    httpd_register_uri_handler(server_, &prev_uri);
    httpd_register_uri_handler(server_, &syncmode_uri);
    httpd_register_uri_handler(server_, &volume_uri);

    // Settings API
    httpd_uri_t startpage_uri = { .uri = "/api/start-page", .method = HTTP_POST, .handler = start_page_handler, .user_ctx = this };
    httpd_register_uri_handler(server_, &startpage_uri);

    // File management endpoints (matching reference repo paths)
    httpd_uri_t download_uri = { .uri = "/download",     .method = HTTP_GET,  .handler = download_handler, .user_ctx = this };
    httpd_uri_t preview_uri  = { .uri = "/preview",      .method = HTTP_GET,  .handler = preview_handler,  .user_ctx = this };
    httpd_uri_t upload_uri   = { .uri = "/upload",       .method = HTTP_POST, .handler = upload_handler,   .user_ctx = this };
    httpd_uri_t delete_uri   = { .uri = "/delete",       .method = HTTP_POST, .handler = delete_handler,   .user_ctx = this };
    httpd_uri_t rename_uri   = { .uri = "/rename",       .method = HTTP_POST, .handler = rename_handler,   .user_ctx = this };
    httpd_uri_t mkdir_uri    = { .uri = "/mkdir",        .method = HTTP_POST, .handler = mkdir_handler,    .user_ctx = this };
    httpd_uri_t storage_uri  = { .uri = "/storage-info", .method = HTTP_GET,  .handler = storage_handler,  .user_ctx = this };

    httpd_register_uri_handler(server_, &download_uri);
    httpd_register_uri_handler(server_, &preview_uri);
    httpd_register_uri_handler(server_, &upload_uri);
    httpd_register_uri_handler(server_, &delete_uri);
    httpd_register_uri_handler(server_, &rename_uri);
    httpd_register_uri_handler(server_, &mkdir_uri);
    httpd_register_uri_handler(server_, &storage_uri);

    ESP_LOGI(TAG, "HTTP server started on port 80");
}

void FileServer::show_connection_info()
{
    show_wifi_qr(display_, config_.ssid, config_.password, config_.url);

    ESP_LOGI(TAG, "Connect to WiFi '%s' (pass: %s), then open http://%s/",
             config_.ssid, config_.password, config_.url);
}

void FileServer::start()
{
    init_wifi_ap();
    start_http_server();
    show_connection_info();
}

// ---- Page handlers ----

esp_err_t FileServer::index_redirect_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    const char *default_page = self->config_.start_page;

    // Build redirect page with configurable default (localStorage overrides if set)
    char html[256];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><script>"
        "var p=localStorage.getItem('startPage');"
        "location.replace(p==='browse'?'/browse':p==='player'?'/player':'/%s');"
        "</script></head><body></body></html>",
        default_page);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, html);
    return ESP_OK;
}

esp_err_t FileServer::player_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, HTML_PLAYER_TEMPLATE);
    return ESP_OK;
}

// ---- SSR Browse handler ----
// Sends HTML_TEMPLATE with markers replaced by dynamic content via chunked response.

static void send_until_marker(httpd_req_t *req, const char *&pos, const char *marker)
{
    const char *found = strstr(pos, marker);
    if (found) {
        httpd_resp_send_chunk(req, pos, found - pos);
        pos = found + strlen(marker);
    }
}

esp_err_t FileServer::browse_handler(httpd_req_t *req)
{
    char query[256] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char path_param[200] = "/";
    get_decoded_query_param(query, "path", path_param, sizeof(path_param));

    // Build SD card directory path
    char dirpath[300];
    snprintf(dirpath, sizeof(dirpath), "%s%s", kSdMountPoint, path_param);

    httpd_resp_set_type(req, "text/html");

    const char *pos = HTML_TEMPLATE;

    // Part 1: HTML head through nav-row opening → up to <!--BACK_BTN-->
    send_until_marker(req, pos, "<!--BACK_BTN-->");

    // Dynamic: back button
    if (strcmp(path_param, "/") != 0) {
        // Compute parent path
        char parent[200];
        strlcpy(parent, path_param, sizeof(parent));
        // Remove trailing slash
        size_t len = strlen(parent);
        if (len > 1 && parent[len - 1] == '/') parent[len - 1] = '\0';
        char *last_slash = strrchr(parent, '/');
        if (last_slash && last_slash != parent) {
            *last_slash = '\0';
        } else {
            strcpy(parent, "/");
        }

        char back_html[300];
        snprintf(back_html, sizeof(back_html),
                 "<a class='back-btn' href='/browse?path=%s'>&#x2190;</a>",
                 parent);
        httpd_resp_sendstr_chunk(req, back_html);
    }

    // Part 2: up to <!--PATH_DISPLAY-->
    send_until_marker(req, pos, "<!--PATH_DISPLAY-->");

    // Dynamic: display path (with spaces around slashes)
    {
        char display_path[400] = "";
        const char *p = path_param;
        char *d = display_path;
        char *end = display_path + sizeof(display_path) - 4;
        while (*p && d < end) {
            if (*p == '/') {
                if (d != display_path) *d++ = ' ';
                *d++ = '/';
                if (*(p + 1)) *d++ = ' ';
            } else {
                *d++ = *p;
            }
            p++;
        }
        *d = '\0';
        httpd_resp_sendstr_chunk(req, display_path);
    }

    // Part 3: up to <!--FILE_LIST-->
    send_until_marker(req, pos, "<!--FILE_LIST-->");

    // Dynamic: file listing
    DIR *dir = opendir(dirpath);
    if (!dir) {
        httpd_resp_sendstr_chunk(req, "<p class='error'>Directory not found</p>");
    } else {
        struct dirent *entry;
        bool has_entries = false;

        // First pass: check if directory is empty (or just scan and generate)
        httpd_resp_sendstr_chunk(req, "<ul>");

        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;

            char fullpath[560];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
            struct stat st;
            stat(fullpath, &st);

            bool is_dir = S_ISDIR(st.st_mode);

            char entry_path[400];
            if (strcmp(path_param, "/") == 0) {
                snprintf(entry_path, sizeof(entry_path), "/%s", entry->d_name);
            } else {
                snprintf(entry_path, sizeof(entry_path), "%s/%s", path_param, entry->d_name);
            }

            char li_buf[800];

            if (is_dir) {
                char date_str[20];
                format_datetime(st.st_mtime, date_str, sizeof(date_str));

                snprintf(li_buf, sizeof(li_buf),
                    "<li class='dir'>"
                    "<a href='/browse?path=%s'>&#x1F4C1; %s/</a>"
                    "<button class='folder-edit-btn' onclick=\"event.stopPropagation();showFolderDialog('%s','%s');\">&#x22ef;</button>"
                    "<div class='file-info'><span class='file-date'>&#x1F4C5; %s</span></div>"
                    "</li>",
                    entry_path, entry->d_name,
                    entry_path, entry->d_name,
                    date_str);
            } else {
                char size_str[20];
                char date_str[20];
                format_size(st.st_size, size_str, sizeof(size_str));
                format_datetime(st.st_mtime, date_str, sizeof(date_str));

                snprintf(li_buf, sizeof(li_buf),
                    "<li>"
                    "<a href='#' onclick=\"showFileDialog('%s','%s')\">%s</a>"
                    "<div class='file-info'><span class='file-size'>&#x1F4BE; %s</span>"
                    "<span class='file-date'>&#x1F4C5; %s</span></div>"
                    "</li>",
                    entry_path, entry->d_name, entry->d_name,
                    size_str, date_str);
            }

            httpd_resp_sendstr_chunk(req, li_buf);
            has_entries = true;
        }

        closedir(dir);

        if (!has_entries) {
            httpd_resp_sendstr_chunk(req, "<li style='text-align:center;color:#868e96'>Empty</li>");
        }

        httpd_resp_sendstr_chunk(req, "</ul>");
    }

    // Part 4: up to __JSPATH__
    send_until_marker(req, pos, "__JSPATH__");

    // Dynamic: JS path value
    httpd_resp_sendstr_chunk(req, path_param);

    // Part 5: remaining template
    httpd_resp_sendstr_chunk(req, pos);

    // End chunked response
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

// ---- Player control handlers ----

esp_err_t FileServer::status_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    auto &ctrl = self->controller_;

    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"playing\":%s,\"file\":\"%s\",\"index\":%d,\"total\":%d,\"folder\":\"%s\",\"sync_mode\":\"%s\",\"volume\":%d}",
             ctrl.is_playing() ? "true" : "false",
             ctrl.current_file(),
             ctrl.current_index(),
             (int)ctrl.playlist().size(),
             ctrl.current_folder().c_str(),
             ctrl.get_audio_priority() ? "audio" : "video",
             ctrl.get_volume());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t FileServer::playlist_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    auto &ctrl = self->controller_;
    auto &playlist = ctrl.playlist();
    auto &subfolders = ctrl.subfolders();

    httpd_resp_set_type(req, "application/json");

    // Send: {"folder":"...","files":[...],"folders":[...]}
    char header[300];
    snprintf(header, sizeof(header), "{\"folder\":\"%s\",\"files\":[", ctrl.current_folder().c_str());
    httpd_resp_sendstr_chunk(req, header);

    for (size_t i = 0; i < playlist.size(); i++) {
        char entry[300];
        snprintf(entry, sizeof(entry), "%s\"%s\"",
                 i > 0 ? "," : "", playlist[i].c_str());
        httpd_resp_sendstr_chunk(req, entry);
    }

    httpd_resp_sendstr_chunk(req, "],\"folders\":[");

    for (size_t i = 0; i < subfolders.size(); i++) {
        char entry[600];
        snprintf(entry, sizeof(entry), "%s{\"name\":\"%s\",\"thumb\":\"%s\"}",
                 i > 0 ? "," : "",
                 subfolders[i].name.c_str(),
                 subfolders[i].thumb.c_str());
        httpd_resp_sendstr_chunk(req, entry);
    }

    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

esp_err_t FileServer::folder_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);

    char query[256] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));

    char name_param[200] = "";
    get_decoded_query_param(query, "name", name_param, sizeof(name_param));

    self->controller_.select_folder(strlen(name_param) > 0 ? name_param : nullptr);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t FileServer::play_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);

    char query[256] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));

    char file_param[200] = "";
    char index_param[16] = "";
    get_decoded_query_param(query, "file", file_param, sizeof(file_param));
    get_decoded_query_param(query, "index", index_param, sizeof(index_param));

    bool ok;
    if (strlen(file_param) > 0) {
        ok = self->controller_.play(file_param);
    } else if (strlen(index_param) > 0) {
        ok = self->controller_.play(atoi(index_param));
    } else {
        ok = self->controller_.play(0);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"play failed\"}");
    return ESP_OK;
}

esp_err_t FileServer::stop_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    self->controller_.stop();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t FileServer::next_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    bool ok = self->controller_.next();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

esp_err_t FileServer::prev_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    bool ok = self->controller_.prev();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

esp_err_t FileServer::sync_mode_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    char query[64] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));

    char mode[16] = "";
    get_decoded_query_param(query, "mode", mode, sizeof(mode));

    if (strcmp(mode, "audio") == 0) {
        self->controller_.set_audio_priority(true);
    } else if (strcmp(mode, "video") == 0) {
        self->controller_.set_audio_priority(false);
    }

    httpd_resp_set_type(req, "application/json");
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"sync_mode\":\"%s\"}",
             self->controller_.get_audio_priority() ? "audio" : "video");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t FileServer::volume_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    char query[64] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));

    char vol_str[8] = "";
    get_decoded_query_param(query, "vol", vol_str, sizeof(vol_str));

    int vol = atoi(vol_str);
    self->controller_.set_volume(vol);

    httpd_resp_set_type(req, "application/json");
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"volume\":%d}", self->controller_.get_volume());
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t FileServer::start_page_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    char query[64] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));

    char page[16] = "";
    get_decoded_query_param(query, "page", page, sizeof(page));

    if (strcmp(page, "player") == 0 || strcmp(page, "browse") == 0) {
        strlcpy(self->config_.start_page, page, sizeof(self->config_.start_page));
        save_server_config("/sdcard/server.config", self->config_);
    }

    httpd_resp_set_type(req, "application/json");
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"start_page\":\"%s\"}", self->config_.start_page);
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// ---- File management handlers ----

const char *FileServer::get_content_type(const char *filepath)
{
    const char *ext = strrchr(filepath, '.');
    if (!ext) return "application/octet-stream";
    ext++;
    if (strcasecmp(ext, "mp4") == 0)  return "video/mp4";
    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) return "text/html";
    if (strcasecmp(ext, "css") == 0)  return "text/css";
    if (strcasecmp(ext, "js") == 0)   return "application/javascript";
    if (strcasecmp(ext, "json") == 0) return "application/json";
    if (strcasecmp(ext, "xml") == 0)  return "text/xml";
    if (strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "csv") == 0 ||
        strcasecmp(ext, "log") == 0 || strcasecmp(ext, "md") == 0 ||
        strcasecmp(ext, "ini") == 0 || strcasecmp(ext, "cfg") == 0 ||
        strcasecmp(ext, "yaml") == 0 || strcasecmp(ext, "yml") == 0 ||
        strcasecmp(ext, "py") == 0 || strcasecmp(ext, "c") == 0 ||
        strcasecmp(ext, "cpp") == 0 || strcasecmp(ext, "h") == 0 ||
        strcasecmp(ext, "hpp") == 0) return "text/plain";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, "png") == 0)  return "image/png";
    if (strcasecmp(ext, "gif") == 0)  return "image/gif";
    if (strcasecmp(ext, "bmp") == 0)  return "image/bmp";
    if (strcasecmp(ext, "webp") == 0) return "image/webp";
    if (strcasecmp(ext, "svg") == 0)  return "image/svg+xml";
    if (strcasecmp(ext, "webm") == 0) return "video/webm";
    if (strcasecmp(ext, "ogg") == 0)  return "video/ogg";
    if (strcasecmp(ext, "mov") == 0)  return "video/quicktime";
    return "application/octet-stream";
}

static esp_err_t stream_file(httpd_req_t *req, const char *filepath)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    char *buf = static_cast<char *>(malloc(kHttpScratchSize));
    if (!buf) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t read_bytes;
    do {
        read_bytes = fread(buf, 1, kHttpScratchSize, f);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
                fclose(f);
                free(buf);
                httpd_resp_sendstr_chunk(req, nullptr);
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);

    fclose(f);
    free(buf);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t FileServer::download_handler(httpd_req_t *req)
{
    char query[256] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char file_param[256] = "";
    get_decoded_query_param(query, "file", file_param, sizeof(file_param));

    char filepath[350];
    snprintf(filepath, sizeof(filepath), "%s%s", kSdMountPoint, file_param);

    // Extract filename for Content-Disposition
    const char *filename = strrchr(file_param, '/');
    filename = filename ? filename + 1 : file_param;

    char disposition[300];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    httpd_resp_set_type(req, "application/octet-stream");

    return stream_file(req, filepath);
}

esp_err_t FileServer::preview_handler(httpd_req_t *req)
{
    char query[256] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char file_param[256] = "";
    get_decoded_query_param(query, "file", file_param, sizeof(file_param));

    char filepath[350];
    snprintf(filepath, sizeof(filepath), "%s%s", kSdMountPoint, file_param);

    httpd_resp_set_hdr(req, "Content-Disposition", "inline");
    httpd_resp_set_type(req, get_content_type(filepath));

    return stream_file(req, filepath);
}

esp_err_t FileServer::upload_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);

    // Block upload during playback (memory constraint)
    if (self->controller_.is_playing()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Cannot upload during playback. Stop the player first.");
        return ESP_FAIL;
    }

    char query[256] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char path_param[200] = "/";
    char filename[200] = "";
    get_decoded_query_param(query, "path", path_param, sizeof(path_param));
    get_decoded_query_param(query, "filename", filename, sizeof(filename));

    if (strlen(filename) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename parameter");
        return ESP_FAIL;
    }

    // Sync time from client
    sync_time_from_query(req, query);

    // Expected size for validation
    char size_param[20] = "";
    get_decoded_query_param(query, "size", size_param, sizeof(size_param));
    size_t expected_size = (size_t)atol(size_param);

    char filepath[400];
    snprintf(filepath, sizeof(filepath), "%s%s%s%s",
             kSdMountPoint, path_param,
             (path_param[strlen(path_param) - 1] == '/') ? "" : "/",
             filename);
    char tmppath[410];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", filepath);

    // Remove existing temp file
    unlink(tmppath);

    ESP_LOGI(TAG, "Upload: path='%s' filename='%s' -> tmppath='%s'", path_param, filename, tmppath);

    FILE *f = fopen(tmppath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "fopen failed for '%s', errno=%d", tmppath, errno);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    char *buf = static_cast<char *>(malloc(kHttpScratchSize));
    if (!buf) {
        fclose(f);
        unlink(tmppath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    size_t total_written = 0;
    ESP_LOGI(TAG, "Upload: %s (%d bytes)", filepath, remaining);

    while (remaining > 0) {
        int recv_size = (remaining < (int)kHttpScratchSize) ? remaining : kHttpScratchSize;
        int received = httpd_req_recv(req, buf, recv_size);

        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "Upload receive error");
            fclose(f);
            free(buf);
            unlink(tmppath);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
            return ESP_FAIL;
        }

        fwrite(buf, 1, received, f);
        total_written += received;
        remaining -= received;
    }

    fclose(f);
    free(buf);

    // Validate size if expected_size was provided
    bool size_ok = (expected_size == 0) || (total_written == expected_size);
    if (size_ok) {
        // Atomic: rename temp to final
        unlink(filepath);  // Remove existing file if any
        rename(tmppath, filepath);
        ESP_LOGI(TAG, "Upload complete: %s (%u bytes)", filepath, (unsigned)total_written);
    } else {
        ESP_LOGE(TAG, "Upload size mismatch (expected %u, got %u), deleting temp",
                 (unsigned)expected_size, (unsigned)total_written);
        unlink(tmppath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload size mismatch");
        return ESP_FAIL;
    }

    // Rescan playlist in case a new .mp4 was uploaded
    self->controller_.rescan();

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static bool recursive_delete(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return false;

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            char child[400];
            snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            recursive_delete(child);
        }
        closedir(dir);
        return rmdir(path) == 0;
    } else {
        return unlink(path) == 0;
    }
}

esp_err_t FileServer::delete_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);

    char query[256] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char file_param[256] = "";
    get_decoded_query_param(query, "file", file_param, sizeof(file_param));

    char filepath[350];
    snprintf(filepath, sizeof(filepath), "%s%s", kSdMountPoint, file_param);

    bool ok = recursive_delete(filepath);

    if (ok) {
        ESP_LOGI(TAG, "Deleted: %s", filepath);
        self->controller_.rescan();
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "OK");
    } else {
        ESP_LOGE(TAG, "Failed to delete: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Delete failed");
    }
    return ESP_OK;
}

esp_err_t FileServer::rename_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);

    char query[512] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char file_param[256] = "";
    char name_param[200] = "";
    get_decoded_query_param(query, "file", file_param, sizeof(file_param));
    get_decoded_query_param(query, "name", name_param, sizeof(name_param));

    char oldpath[350];
    snprintf(oldpath, sizeof(oldpath), "%s%s", kSdMountPoint, file_param);

    // Build new path: same directory + new name
    char newpath[350];
    char *last_slash = strrchr(oldpath, '/');
    if (last_slash) {
        size_t dir_len = last_slash - oldpath + 1;
        snprintf(newpath, sizeof(newpath), "%.*s%s", (int)dir_len, oldpath, name_param);
    } else {
        snprintf(newpath, sizeof(newpath), "%s/%s", kSdMountPoint, name_param);
    }

    bool ok = (::rename(oldpath, newpath) == 0);
    if (ok) {
        ESP_LOGI(TAG, "Renamed: %s -> %s", oldpath, newpath);
        self->controller_.rescan();
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "OK");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Rename failed");
    }
    return ESP_OK;
}

esp_err_t FileServer::mkdir_handler(httpd_req_t *req)
{
    char query[512] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char path_param[200] = "/";
    char name_param[200] = "";
    get_decoded_query_param(query, "path", path_param, sizeof(path_param));
    get_decoded_query_param(query, "name", name_param, sizeof(name_param));

    // Sync time from client
    sync_time_from_query(req, query);

    char dirpath[400];
    snprintf(dirpath, sizeof(dirpath), "%s%s%s%s",
             kSdMountPoint, path_param,
             (path_param[strlen(path_param) - 1] == '/') ? "" : "/",
             name_param);

    ESP_LOGI(TAG, "mkdir: path='%s' name='%s' -> dirpath='%s'", path_param, name_param, dirpath);

    bool ok = (::mkdir(dirpath, 0775) == 0);
    if (ok) {
        ESP_LOGI(TAG, "Created directory: %s", dirpath);
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "OK");
    } else {
        ESP_LOGE(TAG, "mkdir failed for '%s', errno=%d", dirpath, errno);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Create folder failed");
    }
    return ESP_OK;
}

esp_err_t FileServer::storage_handler(httpd_req_t *req)
{
    FATFS *fs;
    DWORD fre_clust;
    FRESULT res = f_getfree("0:", &fre_clust, &fs);

    if (res != FR_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get storage info");
        return ESP_FAIL;
    }

    uint64_t total = (uint64_t)(fs->n_fatent - 2) * fs->csize * 512;
    uint64_t free_bytes = (uint64_t)fre_clust * fs->csize * 512;
    uint64_t used = total - free_bytes;

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"total\":%llu,\"used\":%llu,\"free\":%llu}",
             total, used, free_bytes);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

}  // namespace mp4
