#pragma once

#include <stdint.h>

// I420 (YUV420 Planar) to RGB565 conversion
// stride_w/stride_h: actual decoder output dimensions (macroblock-aligned, multiple of 16)
// width/height: visible video dimensions
static inline void yuv2rgb565(const uint8_t *y_plane, const uint8_t *u_plane, const uint8_t *v_plane,
                              uint16_t *rgb565, int width, int height, int stride_w, int stride_h)
{
    (void)stride_h;
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            int y_idx = j * stride_w + i;
            int uv_idx = (j / 2) * (stride_w / 2) + (i / 2);

            int y = y_plane[y_idx];
            int u = u_plane[uv_idx] - 128;
            int v = v_plane[uv_idx] - 128;

            // ITU-R BT.601 conversion
            int r = y + ((v * 359) >> 8);
            int g = y - ((u * 88 + v * 183) >> 8);
            int b = y + ((u * 454) >> 8);

            // Clamp to [0, 255]
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;

            // Pack to RGB565
            uint16_t pixel = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            rgb565[j * width + i] = pixel;
        }
    }
}

// Convert a full I420 frame to RGB565
// i420_buf: contiguous I420 buffer [Y][U][V] from H.264 decoder
// width/height: visible video dimensions
// H.264 decoder outputs macroblock-aligned frames (multiples of 16)
static inline void i420_to_rgb565(const uint8_t *i420_buf, uint16_t *rgb565, int width, int height)
{
    // H.264 macroblock alignment: dimensions rounded up to multiples of 16
    int stride_w = (width + 15) & ~15;
    int stride_h = (height + 15) & ~15;

    const uint8_t *y_plane = i420_buf;
    const uint8_t *u_plane = i420_buf + stride_w * stride_h;
    const uint8_t *v_plane = u_plane + (stride_w / 2) * (stride_h / 2);
    yuv2rgb565(y_plane, u_plane, v_plane, rgb565, width, height, stride_w, stride_h);
}
