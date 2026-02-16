#include "media_controller.h"

#include <cstring>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "player_constants.h"

static const char *TAG = "media_ctrl";

namespace mp4 {

void MediaController::scan_mp4_files(const char *dirpath)
{
    playlist_.clear();

    DIR *dir = opendir(dirpath);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", dirpath);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip hidden files
        if (entry->d_name[0] == '.') continue;
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
}

static bool is_image_ext(const char *name)
{
    size_t len = strlen(name);
    if (len < 5) return false;
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    ext++;
    return strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0 ||
           strcasecmp(ext, "png") == 0 || strcasecmp(ext, "gif") == 0 ||
           strcasecmp(ext, "bmp") == 0 || strcasecmp(ext, "webp") == 0;
}

// Find the first image file in a directory, return filename or empty string
static std::string find_first_image(const char *dirpath)
{
    DIR *dir = opendir(dirpath);
    if (!dir) return "";

    std::string result;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        if (entry->d_type != DT_REG) continue;
        if (is_image_ext(entry->d_name)) {
            result = entry->d_name;
            break;
        }
    }
    closedir(dir);
    return result;
}

void MediaController::scan_subfolders()
{
    subfolders_.clear();

    std::string playlist_path = std::string(kSdMountPoint) + kPlaylistFolder;
    DIR *dir = opendir(playlist_path.c_str());
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        if (entry->d_type != DT_DIR) continue;

        FolderInfo info;
        info.name = entry->d_name;

        // Find thumbnail image in subfolder
        std::string subdir = playlist_path + "/" + entry->d_name;
        std::string img = find_first_image(subdir.c_str());
        if (!img.empty()) {
            info.thumb = std::string(kPlaylistFolder) + "/" + entry->d_name + "/" + img;
        }

        subfolders_.push_back(std::move(info));
    }
    closedir(dir);

    std::sort(subfolders_.begin(), subfolders_.end(),
              [](const FolderInfo &a, const FolderInfo &b) { return a.name < b.name; });

    ESP_LOGI(TAG, "Found %d subfolders in PLAYLIST", (int)subfolders_.size());
    for (size_t i = 0; i < subfolders_.size(); i++) {
        ESP_LOGI(TAG, "  [%d] %s/ (thumb: %s)", (int)i,
                 subfolders_[i].name.c_str(),
                 subfolders_[i].thumb.empty() ? "none" : subfolders_[i].thumb.c_str());
    }
}

void MediaController::scan_playlist()
{
    playlist_.clear();
    subfolders_.clear();
    current_folder_.clear();

    std::string playlist_path = std::string(kSdMountPoint) + kPlaylistFolder;

    // Create PLAYLIST folder if it doesn't exist
    struct stat st;
    if (stat(playlist_path.c_str(), &st) != 0) {
        mkdir(playlist_path.c_str(), 0755);
        ESP_LOGI(TAG, "Created playlist directory: %s", playlist_path.c_str());
        return;
    }

    // Scan for mp4 files in PLAYLIST root
    scan_mp4_files(playlist_path.c_str());

    if (!playlist_.empty()) {
        ESP_LOGI(TAG, "Found %d MP4 files in PLAYLIST", (int)playlist_.size());
        for (size_t i = 0; i < playlist_.size(); i++) {
            ESP_LOGI(TAG, "  [%d] %s", (int)i, playlist_[i].c_str());
        }
        return;
    }

    // No mp4 files in root — scan for subfolders
    scan_subfolders();
}

void MediaController::rescan()
{
    // Re-scan preserving current folder selection
    std::string saved_folder = current_folder_;
    scan_playlist();

    // If we had a subfolder selected and it still exists, re-select it
    if (!saved_folder.empty() && !subfolders_.empty()) {
        for (const auto &f : subfolders_) {
            if (f.name == saved_folder) {
                select_folder(saved_folder.c_str());
                return;
            }
        }
    }
}

void MediaController::select_folder(const char *name)
{
    current_folder_ = name ? name : "";
    playlist_.clear();

    std::string path = std::string(kSdMountPoint) + kPlaylistFolder;
    if (!current_folder_.empty()) {
        path += "/" + current_folder_;
    }

    scan_mp4_files(path.c_str());
    ESP_LOGI(TAG, "Selected folder '%s': %d MP4 files",
             current_folder_.empty() ? "(root)" : current_folder_.c_str(),
             (int)playlist_.size());
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
    std::string filepath = std::string(kSdMountPoint) + kPlaylistFolder;
    if (!current_folder_.empty()) {
        filepath += "/" + current_folder_;
    }
    filepath += "/" + playlist_[index];
    ESP_LOGI(TAG, "Playing [%d]: %s", index, filepath.c_str());

    // Filepath must persist — store in a static buffer
    static char path_buf[256];
    snprintf(path_buf, sizeof(path_buf), "%s", filepath.c_str());

    player_ = new Mp4Player(display_, path_buf);
    player_->set_audio_priority(audio_priority_);
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

        // Auto-advance to next file in playlist
        if (!playlist_.empty()) {
            int next_idx = current_index_ + 1;
            if (next_idx < (int)playlist_.size()) {
                ESP_LOGI(TAG, "Auto-advancing to next: [%d]", next_idx);
                play(next_idx);
            } else {
                ESP_LOGI(TAG, "Playlist finished (reached end)");
            }
        }
    }
}

}  // namespace mp4
