#pragma once

#include <string>
#include <vector>
#include "lcd_config.h"
#include "mp4_player.h"

namespace mp4 {

class MediaController {
public:
    MediaController(LGFX &display) : display_(display) {}

    // Playlist
    void scan_playlist();
    const std::vector<std::string> &playlist() const { return playlist_; }

    // Playback controls
    bool play(int index);
    bool play(const char *filename);
    void stop();
    bool next();
    bool prev();

    // State
    bool is_playing() const;
    int current_index() const { return current_index_; }
    const char *current_file() const;

    // Call from main loop to detect playback completion
    void tick();

private:
    void stop_and_wait();

    LGFX &display_;
    std::vector<std::string> playlist_;
    int current_index_ = -1;

    Mp4Player *player_ = nullptr;
};

}  // namespace mp4
