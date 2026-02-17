#pragma once

#include <cstring>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lcd_config.h"
#include "mp4_player.h"

namespace mp4 {

// Player settings, loadable from /sdcard/playlist/player.config
struct PlayerConfig {
    int volume;          // 0–100
    char sync_mode[8];   // "audio" or "video"
    char folder[64];     // playlist subfolder name (empty = root)
    bool repeat;         // loop playlist when reaching end

    PlayerConfig() : volume(100), repeat(false) {
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

// Commands posted from HTTP handlers, processed by tick() on main thread
enum class CmdType : uint8_t {
    PlayIndex,
    PlayFile,
    Stop,
    Next,
    Prev,
};

struct PlayerCmd {
    CmdType type;
    int index;
    char filename[64];
};

class MediaController {
public:
    MediaController(LGFX &display, const PlayerConfig &config)
        : display_(display), player_config_(config)
        , audio_priority_(strcmp(config.sync_mode, "video") != 0)
        , repeat_(config.repeat)
        , volume_(config.volume)
        , cmd_queue_(xQueueCreate(4, sizeof(PlayerCmd))) {}

    // Playlist (main thread only)
    void scan_playlist();
    void rescan();
    void select_folder(const char *name);
    const std::vector<std::string> &playlist() const { return playlist_; }
    const std::vector<FolderInfo> &subfolders() const { return subfolders_; }
    const std::string &current_folder() const { return current_folder_; }
    const std::string &playing_folder() const { return playing_folder_; }

    // Thread-safe command posting (called from HTTP handlers)
    void post_play(int index);
    void post_play_file(const char *filename);
    void post_stop();
    void post_next();
    void post_prev();

    // Direct playback (main thread only, used by app_main)
    bool play(int index);

    // Sync mode
    void set_audio_priority(bool v) { audio_priority_ = v; }
    bool get_audio_priority() const { return audio_priority_; }

    // Repeat
    void set_repeat(bool v) { repeat_ = v; }
    bool get_repeat() const { return repeat_; }

    // Volume (0–100), safe from any thread (volatile write)
    void set_volume(int vol);
    int get_volume() const { return volume_; }

    // State queries (safe from any thread via simple flag reads)
    bool is_playing() const { return playing_; }
    int current_index() const { return current_index_; }
    const char *current_file() const;

    // Saved default folder from player.config
    const char *saved_folder() const { return player_config_.folder; }

    // Persist current settings to player.config
    void save_config();

    // Call from main loop — processes queued commands and detects playback completion
    void tick();

private:
    void process_commands();
    bool play_internal(int index);
    bool play_internal_by_name(const char *filename);
    void stop_internal();
    void stop_and_wait();
    bool next_internal();
    bool prev_internal();
    void scan_mp4_files(const char *dirpath);
    void scan_subfolders();

    LGFX &display_;
    PlayerConfig player_config_;
    std::vector<std::string> playlist_;
    std::vector<FolderInfo> subfolders_;
    std::string current_folder_;
    std::string playing_folder_;
    std::string playing_file_;
    int current_index_ = -1;
    bool audio_priority_ = true;
    bool repeat_ = false;
    volatile bool playing_ = false;
    bool user_stopped_ = false;
    int volume_ = 100;

    QueueHandle_t cmd_queue_ = nullptr;
    Mp4Player *player_ = nullptr;
};

}  // namespace mp4
