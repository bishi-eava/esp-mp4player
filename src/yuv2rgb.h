#pragma once

#include <stdint.h>

namespace mp4 {

// Single BT.601 YUV→RGB565 pixel conversion core
static inline uint16_t yuv_to_rgb565(int y, int u, int v)
{
    int r = y + ((v * 359) >> 8);
    int g = y - ((u * 88 + v * 183) >> 8);
    int b = y + ((u * 454) >> 8);

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;

    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// H.264 macroblock alignment: round up to multiple of 16
static inline int mb_align(int dim) { return (dim + 15) & ~15; }

// Convert a full I420 frame to RGB565 (1:1, no scaling)
// i420_buf: contiguous I420 buffer [Y][U][V] from H.264 decoder
// width/height: visible video dimensions
static inline void i420_to_rgb565(const uint8_t *i420_buf, uint16_t *rgb565,
                                   int width, int height)
{
    int stride_w = mb_align(width);
    int stride_h = mb_align(height);

    const uint8_t *y_plane = i420_buf;
    const uint8_t *u_plane = i420_buf + stride_w * stride_h;
    const uint8_t *v_plane = u_plane + (stride_w / 2) * (stride_h / 2);
    int half_stride = stride_w / 2;

    for (int j = 0; j < height; j++) {
        const uint8_t *y_row = y_plane + j * stride_w;
        const uint8_t *u_row = u_plane + (j / 2) * half_stride;
        const uint8_t *v_row = v_plane + (j / 2) * half_stride;

        for (int i = 0; i < width; i++) {
            rgb565[j * width + i] = yuv_to_rgb565(
                y_row[i], u_row[i / 2] - 128, v_row[i / 2] - 128);
        }
    }
}

// Convert I420 to RGB565 with nearest-neighbor downscaling
// src_w/src_h: original decoder output dimensions
// dst_w/dst_h: scaled output dimensions (must be <= src_w/src_h)
static inline void i420_to_rgb565_scaled(const uint8_t *i420_buf, uint16_t *rgb565,
                                          int src_w, int src_h, int dst_w, int dst_h)
{
    int stride_w = mb_align(src_w);
    int stride_h = mb_align(src_h);

    const uint8_t *y_plane = i420_buf;
    const uint8_t *u_plane = i420_buf + stride_w * stride_h;
    const uint8_t *v_plane = u_plane + (stride_w / 2) * (stride_h / 2);
    int half_stride = stride_w / 2;

    for (int j = 0; j < dst_h; j++) {
        int src_y = j * src_h / dst_h;
        const uint8_t *y_row = y_plane + src_y * stride_w;
        const uint8_t *u_row = u_plane + (src_y / 2) * half_stride;
        const uint8_t *v_row = v_plane + (src_y / 2) * half_stride;

        for (int i = 0; i < dst_w; i++) {
            int src_x = i * src_w / dst_w;
            rgb565[j * dst_w + i] = yuv_to_rgb565(
                y_row[src_x], u_row[src_x / 2] - 128, v_row[src_x / 2] - 128);
        }
    }
}

}  // namespace mp4
