#pragma once

#include "lcd_config.h"

// Play an MP4 (H.264) file and render to the display
// display: LovyanGFX display instance
// filepath: path to the MP4 file on the filesystem
void play_mp4(LGFX &display, const char *filepath);
