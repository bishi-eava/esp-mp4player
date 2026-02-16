#include "wifi_file_server.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>

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
    strlcpy((char *)wifi_config.ap.ssid, kApSsid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(kApSsid);
    wifi_config.ap.channel = kApChannel;
    wifi_config.ap.max_connection = kApMaxConnections;

    if (strlen(kApPassword) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strlcpy((char *)wifi_config.ap.password, kApPassword, sizeof(wifi_config.ap.password));
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s, Channel=%d", kApSsid, kApChannel);
}

// ---- HTTP server setup ----

void FileServer::start_http_server()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = kHttpServerStack;
    config.max_uri_handlers = kHttpMaxUriHandlers;
    config.max_open_sockets = 3;

    ESP_ERROR_CHECK(httpd_start(&server_, &config));

    // UI
    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = this };
    httpd_register_uri_handler(server_, &index_uri);

    // Player control API
    httpd_uri_t status_uri   = { .uri = "/api/status",   .method = HTTP_GET,  .handler = status_handler,   .user_ctx = this };
    httpd_uri_t playlist_uri = { .uri = "/api/playlist", .method = HTTP_GET,  .handler = playlist_handler, .user_ctx = this };
    httpd_uri_t play_uri     = { .uri = "/api/play",     .method = HTTP_POST, .handler = play_handler,     .user_ctx = this };
    httpd_uri_t stop_uri     = { .uri = "/api/stop",     .method = HTTP_POST, .handler = stop_handler,     .user_ctx = this };
    httpd_uri_t next_uri     = { .uri = "/api/next",     .method = HTTP_POST, .handler = next_handler,     .user_ctx = this };
    httpd_uri_t prev_uri     = { .uri = "/api/prev",     .method = HTTP_POST, .handler = prev_handler,     .user_ctx = this };

    httpd_register_uri_handler(server_, &status_uri);
    httpd_register_uri_handler(server_, &playlist_uri);
    httpd_register_uri_handler(server_, &play_uri);
    httpd_register_uri_handler(server_, &stop_uri);
    httpd_register_uri_handler(server_, &next_uri);
    httpd_register_uri_handler(server_, &prev_uri);

    // File management API
    httpd_uri_t files_uri    = { .uri = "/api/files",    .method = HTTP_GET,  .handler = files_handler,    .user_ctx = this };
    httpd_uri_t download_uri = { .uri = "/api/download", .method = HTTP_GET,  .handler = download_handler, .user_ctx = this };
    httpd_uri_t upload_uri   = { .uri = "/api/upload",   .method = HTTP_POST, .handler = upload_handler,   .user_ctx = this };
    httpd_uri_t delete_uri   = { .uri = "/api/delete",   .method = HTTP_POST, .handler = delete_handler,   .user_ctx = this };
    httpd_uri_t rename_uri   = { .uri = "/api/rename",   .method = HTTP_POST, .handler = rename_handler,   .user_ctx = this };
    httpd_uri_t mkdir_uri    = { .uri = "/api/mkdir",    .method = HTTP_POST, .handler = mkdir_handler,    .user_ctx = this };
    httpd_uri_t storage_uri  = { .uri = "/api/storage",  .method = HTTP_GET,  .handler = storage_handler,  .user_ctx = this };

    httpd_register_uri_handler(server_, &files_uri);
    httpd_register_uri_handler(server_, &download_uri);
    httpd_register_uri_handler(server_, &upload_uri);
    httpd_register_uri_handler(server_, &delete_uri);
    httpd_register_uri_handler(server_, &rename_uri);
    httpd_register_uri_handler(server_, &mkdir_uri);
    httpd_register_uri_handler(server_, &storage_uri);

    ESP_LOGI(TAG, "HTTP server started on port 80");
}

void FileServer::show_connection_info()
{
    show_wifi_qr(display_, kApSsid, kApPassword, "192.168.4.1");

    ESP_LOGI(TAG, "Connect to WiFi '%s' (pass: %s), then open http://192.168.4.1/",
             kApSsid, kApPassword);
}

void FileServer::start()
{
    init_wifi_ap();
    start_http_server();
    show_connection_info();
}

// ---- UI handler ----

esp_err_t FileServer::index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ---- Player control handlers ----

esp_err_t FileServer::status_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    auto &ctrl = self->controller_;

    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"playing\":%s,\"file\":\"%s\",\"index\":%d,\"total\":%d}",
             ctrl.is_playing() ? "true" : "false",
             ctrl.current_file(),
             ctrl.current_index(),
             (int)ctrl.playlist().size());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t FileServer::playlist_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);
    auto &playlist = self->controller_.playlist();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[");

    for (size_t i = 0; i < playlist.size(); i++) {
        char entry[300];
        snprintf(entry, sizeof(entry), "%s\"%s\"",
                 i > 0 ? "," : "", playlist[i].c_str());
        httpd_resp_sendstr_chunk(req, entry);
    }

    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

esp_err_t FileServer::play_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);

    char query[256] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));

    char file_param[200] = "";
    char index_param[16] = "";
    httpd_query_key_value(query, "file", file_param, sizeof(file_param));
    httpd_query_key_value(query, "index", index_param, sizeof(index_param));

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

// ---- File management handlers ----

esp_err_t FileServer::files_handler(httpd_req_t *req)
{
    char query[256] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char path_param[200] = "/";
    httpd_query_key_value(query, "path", path_param, sizeof(path_param));

    char dirpath[300];
    snprintf(dirpath, sizeof(dirpath), "%s%s", kSdMountPoint, path_param);

    DIR *dir = opendir(dirpath);
    if (!dir) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[");

    struct dirent *entry;
    bool first = true;
    char entry_buf[400];

    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        char fullpath[560];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
        struct stat st;
        stat(fullpath, &st);

        bool is_dir = S_ISDIR(st.st_mode);
        snprintf(entry_buf, sizeof(entry_buf),
                 "%s{\"name\":\"%s\",\"size\":%ld,\"dir\":%s}",
                 first ? "" : ",",
                 entry->d_name,
                 (long)st.st_size,
                 is_dir ? "true" : "false");
        httpd_resp_sendstr_chunk(req, entry_buf);
        first = false;
    }

    closedir(dir);
    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

const char *FileServer::get_content_type(const char *filepath)
{
    const char *ext = strrchr(filepath, '.');
    if (!ext) return "application/octet-stream";
    ext++;
    if (strcasecmp(ext, "mp4") == 0)  return "video/mp4";
    if (strcasecmp(ext, "html") == 0) return "text/html";
    if (strcasecmp(ext, "css") == 0)  return "text/css";
    if (strcasecmp(ext, "js") == 0)   return "application/javascript";
    if (strcasecmp(ext, "json") == 0) return "application/json";
    if (strcasecmp(ext, "txt") == 0)  return "text/plain";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, "png") == 0)  return "image/png";
    if (strcasecmp(ext, "gif") == 0)  return "image/gif";
    return "application/octet-stream";
}

esp_err_t FileServer::download_handler(httpd_req_t *req)
{
    char query[256] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char path_param[256] = "";
    httpd_query_key_value(query, "path", path_param, sizeof(path_param));

    char filepath[350];
    snprintf(filepath, sizeof(filepath), "%s%s", kSdMountPoint, path_param);

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_content_type(filepath));

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
    httpd_query_key_value(query, "path", path_param, sizeof(path_param));
    httpd_query_key_value(query, "filename", filename, sizeof(filename));

    if (strlen(filename) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename parameter");
        return ESP_FAIL;
    }

    char filepath[400];
    snprintf(filepath, sizeof(filepath), "%s%s%s", kSdMountPoint, path_param, filename);
    char tmppath[410];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", filepath);

    FILE *f = fopen(tmppath, "wb");
    if (!f) {
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
        remaining -= received;
    }

    fclose(f);
    free(buf);

    // Atomic: rename temp to final
    unlink(filepath);  // Remove existing file if any
    rename(tmppath, filepath);

    ESP_LOGI(TAG, "Upload complete: %s (%d bytes)", filepath, req->content_len);

    // Rescan playlist in case a new .mp4 was uploaded
    self->controller_.scan_playlist();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
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
    char path_param[256] = "";
    httpd_query_key_value(query, "path", path_param, sizeof(path_param));

    char filepath[350];
    snprintf(filepath, sizeof(filepath), "%s%s", kSdMountPoint, path_param);

    bool ok = recursive_delete(filepath);

    if (ok) {
        ESP_LOGI(TAG, "Deleted: %s", filepath);
        self->controller_.scan_playlist();
    } else {
        ESP_LOGE(TAG, "Failed to delete: %s", filepath);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

esp_err_t FileServer::rename_handler(httpd_req_t *req)
{
    auto *self = static_cast<FileServer *>(req->user_ctx);

    char query[512] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char path_param[256] = "";
    char name_param[200] = "";
    httpd_query_key_value(query, "path", path_param, sizeof(path_param));
    httpd_query_key_value(query, "name", name_param, sizeof(name_param));

    char oldpath[350];
    snprintf(oldpath, sizeof(oldpath), "%s%s", kSdMountPoint, path_param);

    // Build new path: same directory + new name
    char newpath[350];
    char *last_slash = strrchr(oldpath, '/');
    if (last_slash) {
        size_t dir_len = last_slash - oldpath + 1;
        snprintf(newpath, sizeof(newpath), "%.*s%s", (int)dir_len, oldpath, name_param);
    } else {
        snprintf(newpath, sizeof(newpath), "%s/%s", kSdMountPoint, name_param);
    }

    bool ok = (rename(oldpath, newpath) == 0);
    if (ok) {
        ESP_LOGI(TAG, "Renamed: %s -> %s", oldpath, newpath);
        self->controller_.scan_playlist();
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

esp_err_t FileServer::mkdir_handler(httpd_req_t *req)
{
    char query[512] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char path_param[200] = "/";
    char name_param[200] = "";
    httpd_query_key_value(query, "path", path_param, sizeof(path_param));
    httpd_query_key_value(query, "name", name_param, sizeof(name_param));

    char dirpath[400];
    snprintf(dirpath, sizeof(dirpath), "%s%s%s", kSdMountPoint, path_param, name_param);

    bool ok = (mkdir(dirpath, 0775) == 0);
    if (ok) ESP_LOGI(TAG, "Created directory: %s", dirpath);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? "{\"ok\":true}" : "{\"ok\":false}");
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
