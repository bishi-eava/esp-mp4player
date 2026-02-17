#pragma once

#include <cstring>
#include <string>
#include <vector>
#include "lcd_config.h"
#include "mp4_player.h"

namespace mp4 {

// Player settings, loadable from /sdcard/playlist/player.config
struct PlayerConfig {
    int volume;          // 0–100
    char sync_mode[8];   // "audio" or "video"
    char folder[64];     // playlist subfolder name (empty = root)

    PlayerConfig() : volume(100) {
        strlcpy(sync_mode, "audio", sizeof(sync_mode));
        folder[0] = '\0';
    }
};

PlayerConfig load_player_config(const char *path);
void save_player_config(const char *path, const PlayerConfig &cfg);

struct FolderInfo {
    std::string name;
    std::string thumb;  // relative path to first image (e.g. "/PLAYLIST/sub/img.jpg"), empty if none
};

class MediaController {
public:
    MediaController(LGFX &display, const PlayerConfig &config)
        : display_(display), player_config_(config)
        , audio_priority_(strcmp(config.sync_mode, "video") != 0)
        , volume_(config.volume) {}

    // Playlist
    void scan_playlist();
    void rescan();
    void select_folder(const char *name);
    const std::vector<std::string> &playlist() const { return playlist_; }
    const std::vector<FolderInfo> &subfolders() const { return subfolders_; }
    const std::string &current_folder() const { return current_folder_; }

    // Playback controls
    bool play(int index);
    bool play(const char *filename);
    void stop();
    bool next();
    bool prev();

    // Sync mode
    void set_audio_priority(bool v) { audio_priority_ = v; }
    bool get_audio_priority() const { return audio_priority_; }

    // Volume (0–100)
    void set_volume(int vol);
    int get_volume() const { return volume_; }

    // State
    bool is_playing() const;
    int current_index() const { return current_index_; }
    const char *current_file() const;

    // Saved default folder from player.config
    const char *saved_folder() const { return player_config_.folder; }

    // Persist current settings to player.config
    void save_config();

    // Call from main loop to detect playback completion
    void tick();

private:
    void stop_and_wait();
    void scan_mp4_files(const char *dirpath);
    void scan_subfolders();

    LGFX &display_;
    PlayerConfig player_config_;
    std::vector<std::string> playlist_;
    std::vector<FolderInfo> subfolders_;
    std::string current_folder_;
    int current_index_ = -1;
    bool audio_priority_ = true;
    bool user_stopped_ = false;
    int volume_ = 100;

    Mp4Player *player_ = nullptr;
};

}  // namespace mp4
