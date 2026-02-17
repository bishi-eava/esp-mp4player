#include "media_controller.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "player_constants.h"

static const char *TAG = "media_ctrl";

namespace mp4 {

// ---- Player config loader/saver ----

static char *trim_line(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) *end-- = '\0';
    return s;
}

PlayerConfig load_player_config(const char *path)
{
    PlayerConfig cfg;

    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGI(TAG, "No player config at %s, using defaults", path);
        return cfg;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim_line(line);
        if (*p == '\0' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim_line(p);
        char *val = trim_line(eq + 1);

        if (strcmp(key, "volume") == 0) {
            int v = atoi(val);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            cfg.volume = v;
        } else if (strcmp(key, "sync_mode") == 0) {
            if (strcmp(val, "audio") == 0 || strcmp(val, "video") == 0) {
                strlcpy(cfg.sync_mode, val, sizeof(cfg.sync_mode));
            }
        } else if (strcmp(key, "folder") == 0) {
            strlcpy(cfg.folder, val, sizeof(cfg.folder));
        } else if (strcmp(key, "repeat") == 0) {
            cfg.repeat = (strcmp(val, "on") == 0);
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "Loaded player config: volume=%d, sync_mode=%s, folder=%s, repeat=%s",
             cfg.volume, cfg.sync_mode, cfg.folder[0] ? cfg.folder : "(root)",
             cfg.repeat ? "on" : "off");
    return cfg;
}

void save_player_config(const char *path, const PlayerConfig &cfg)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to write player config to %s", path);
        return;
    }
    fprintf(f, "volume=%d\nsync_mode=%s\nfolder=%s\nrepeat=%s\n",
            cfg.volume, cfg.sync_mode, cfg.folder,
            cfg.repeat ? "on" : "off");
    fclose(f);
    ESP_LOGI(TAG, "Saved player config to %s", path);
}

void MediaController::save_config()
{
    player_config_.volume = volume_;
    strlcpy(player_config_.sync_mode,
            audio_priority_ ? "audio" : "video",
            sizeof(player_config_.sync_mode));
    strlcpy(player_config_.folder,
            current_folder_.c_str(),
            sizeof(player_config_.folder));
    player_config_.repeat = repeat_;
    save_player_config("/sdcard/playlist/player.config", player_config_);
}

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

// --- Thread-safe command posting (called from HTTP handlers) ---

void MediaController::post_play(int index)
{
    PlayerCmd cmd = {};
    cmd.type = CmdType::PlayIndex;
    cmd.index = index;
    xQueueSend(cmd_queue_, &cmd, 0);
}

void MediaController::post_play_file(const char *filename)
{
    PlayerCmd cmd = {};
    cmd.type = CmdType::PlayFile;
    strlcpy(cmd.filename, filename, sizeof(cmd.filename));
    xQueueSend(cmd_queue_, &cmd, 0);
}

void MediaController::post_stop()
{
    PlayerCmd cmd = {};
    cmd.type = CmdType::Stop;
    xQueueSend(cmd_queue_, &cmd, 0);
}

void MediaController::post_next()
{
    PlayerCmd cmd = {};
    cmd.type = CmdType::Next;
    xQueueSend(cmd_queue_, &cmd, 0);
}

void MediaController::post_prev()
{
    PlayerCmd cmd = {};
    cmd.type = CmdType::Prev;
    xQueueSend(cmd_queue_, &cmd, 0);
}

// --- Direct playback (main thread only, used by app_main) ---

bool MediaController::play(int index)
{
    return play_internal(index);
}

// --- Internal playback controls (main thread only) ---

bool MediaController::play_internal(int index)
{
    if (index < 0 || index >= (int)playlist_.size()) {
        ESP_LOGE(TAG, "Invalid playlist index: %d", index);
        return false;
    }
    playing_playlist_ = playlist_;
    return start_playback(index, current_folder_, playlist_[index]);
}

bool MediaController::start_playback(int index, const std::string &folder, const std::string &filename)
{
    stop_and_wait();

    current_index_ = index;
    playing_folder_ = folder;
    playing_file_ = filename;

    std::string filepath = std::string(kSdMountPoint) + kPlaylistFolder;
    if (!folder.empty()) filepath += "/" + folder;
    filepath += "/" + filename;
    ESP_LOGI(TAG, "Playing [%d]: %s", index, filepath.c_str());

    static char path_buf[256];
    snprintf(path_buf, sizeof(path_buf), "%s", filepath.c_str());

    player_ = new Mp4Player(display_, path_buf);
    player_->set_audio_priority(audio_priority_);
    player_->set_volume(volume_);
    player_->start();
    playing_ = true;
    return true;
}

bool MediaController::play_internal_by_name(const char *filename)
{
    for (int i = 0; i < (int)playlist_.size(); i++) {
        if (playlist_[i] == filename) {
            return play_internal(i);
        }
    }
    ESP_LOGE(TAG, "File not in playlist: %s", filename);
    return false;
}

void MediaController::stop_internal()
{
    if (player_) {
        ESP_LOGI(TAG, "Stop requested");
        user_stopped_ = true;
        stop_and_wait();
        playing_ = false;
        playing_file_.clear();
    }
}

void MediaController::set_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    volume_ = vol;
    if (player_) {
        player_->set_volume(vol);
    }
}

void MediaController::stop_and_wait()
{
    if (!player_) return;

    player_->request_stop();
    player_->wait_until_finished();
    delete player_;
    player_ = nullptr;
    playing_ = false;
    ESP_LOGI(TAG, "Player stopped and cleaned up");
}

bool MediaController::next_internal()
{
    if (playlist_.empty()) return false;
    int next_idx = (current_index_ + 1) % (int)playlist_.size();
    return play_internal(next_idx);
}

bool MediaController::prev_internal()
{
    if (playlist_.empty()) return false;
    int prev_idx = current_index_ - 1;
    if (prev_idx < 0) prev_idx = (int)playlist_.size() - 1;
    return play_internal(prev_idx);
}

const char *MediaController::current_file() const
{
    return playing_file_.c_str();
}

void MediaController::process_commands()
{
    PlayerCmd cmd;
    while (xQueueReceive(cmd_queue_, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
        case CmdType::PlayIndex:
            user_stopped_ = false;
            play_internal(cmd.index);
            break;
        case CmdType::PlayFile:
            user_stopped_ = false;
            play_internal_by_name(cmd.filename);
            break;
        case CmdType::Stop:
            stop_internal();
            break;
        case CmdType::Next:
            user_stopped_ = false;
            next_internal();
            break;
        case CmdType::Prev:
            user_stopped_ = false;
            prev_internal();
            break;
        }
    }
}

void MediaController::tick()
{
    // Process commands from HTTP handlers (main thread only)
    process_commands();

    if (player_ && player_->is_finished()) {
        ESP_LOGI(TAG, "Playback finished: %s", current_file());
        // Clean up the finished player
        player_->wait_until_finished();
        delete player_;
        player_ = nullptr;
        playing_ = false;

        // Don't auto-advance if user explicitly stopped
        if (user_stopped_) {
            user_stopped_ = false;
            ESP_LOGI(TAG, "User stopped, not auto-advancing");
            return;
        }

        // Auto-advance to next file in playing playlist
        if (!playing_playlist_.empty()) {
            int next_idx = current_index_ + 1;
            if (next_idx < (int)playing_playlist_.size()) {
                ESP_LOGI(TAG, "Auto-advancing to next: [%d]", next_idx);
                start_playback(next_idx, playing_folder_, playing_playlist_[next_idx]);
            } else if (repeat_) {
                ESP_LOGI(TAG, "Repeating playlist from beginning");
                start_playback(0, playing_folder_, playing_playlist_[0]);
            } else {
                ESP_LOGI(TAG, "Playlist finished (reached end)");
                playing_file_.clear();
            }
        }
    }
}

}  // namespace mp4
