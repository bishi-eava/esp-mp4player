#pragma once

#include <string>
#include <vector>
#include "lcd_config.h"
#include "mp4_player.h"

namespace mp4 {

struct FolderInfo {
    std::string name;
    std::string thumb;  // relative path to first image (e.g. "/PLAYLIST/sub/img.jpg"), empty if none
};

class MediaController {
public:
    MediaController(LGFX &display) : display_(display) {}

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

    // State
    bool is_playing() const;
    int current_index() const { return current_index_; }
    const char *current_file() const;

    // Call from main loop to detect playback completion
    void tick();

private:
    void stop_and_wait();
    void scan_mp4_files(const char *dirpath);
    void scan_subfolders();

    LGFX &display_;
    std::vector<std::string> playlist_;
    std::vector<FolderInfo> subfolders_;
    std::string current_folder_;
    int current_index_ = -1;
    bool audio_priority_ = true;
    bool user_stopped_ = false;

    Mp4Player *player_ = nullptr;
};

}  // namespace mp4
