#include "media_controller.h"

#include <cstring>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "player_constants.h"

static const char *TAG = "media_ctrl";

namespace mp4 {

void MediaController::scan_playlist()
{
    playlist_.clear();

    DIR *dir = opendir(kSdMountPoint);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open SD card directory: %s", kSdMountPoint);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) continue;

        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len < 5) continue;

        // Check for .mp4 extension (case-insensitive)
        const char *ext = name + len - 4;
        if (strcasecmp(ext, ".mp4") != 0) continue;

        playlist_.push_back(name);
    }
    closedir(dir);

    std::sort(playlist_.begin(), playlist_.end());
    ESP_LOGI(TAG, "Found %d MP4 files on SD card", (int)playlist_.size());
    for (size_t i = 0; i < playlist_.size(); i++) {
        ESP_LOGI(TAG, "  [%d] %s", (int)i, playlist_[i].c_str());
    }
}

bool MediaController::play(int index)
{
    if (index < 0 || index >= (int)playlist_.size()) {
        ESP_LOGE(TAG, "Invalid playlist index: %d", index);
        return false;
    }

    // Stop current playback if any
    if (player_) {
        stop_and_wait();
    }

    current_index_ = index;
    std::string filepath = std::string(kSdMountPoint) + "/" + playlist_[index];
    ESP_LOGI(TAG, "Playing [%d]: %s", index, filepath.c_str());

    // Filepath must persist — store in a static buffer
    static char path_buf[256];
    snprintf(path_buf, sizeof(path_buf), "%s", filepath.c_str());

    player_ = new Mp4Player(display_, path_buf);
    player_->start();
    return true;
}

bool MediaController::play(const char *filename)
{
    for (int i = 0; i < (int)playlist_.size(); i++) {
        if (playlist_[i] == filename) {
            return play(i);
        }
    }
    ESP_LOGE(TAG, "File not in playlist: %s", filename);
    return false;
}

void MediaController::stop()
{
    if (player_) {
        ESP_LOGI(TAG, "Stop requested");
        player_->request_stop();
    }
}

void MediaController::stop_and_wait()
{
    if (!player_) return;

    player_->request_stop();
    player_->wait_until_finished();
    delete player_;
    player_ = nullptr;
    ESP_LOGI(TAG, "Player stopped and cleaned up");
}

bool MediaController::next()
{
    if (playlist_.empty()) return false;
    int next_idx = (current_index_ + 1) % (int)playlist_.size();
    return play(next_idx);
}

bool MediaController::prev()
{
    if (playlist_.empty()) return false;
    int prev_idx = current_index_ - 1;
    if (prev_idx < 0) prev_idx = (int)playlist_.size() - 1;
    return play(prev_idx);
}

bool MediaController::is_playing() const
{
    return player_ && !player_->is_finished();
}

const char *MediaController::current_file() const
{
    if (current_index_ < 0 || current_index_ >= (int)playlist_.size()) return "";
    return playlist_[current_index_].c_str();
}

void MediaController::tick()
{
    if (player_ && player_->is_finished()) {
        ESP_LOGI(TAG, "Playback finished: %s", current_file());
        // Clean up the finished player
        player_->wait_until_finished();
        delete player_;
        player_ = nullptr;
    }
}

}  // namespace mp4
