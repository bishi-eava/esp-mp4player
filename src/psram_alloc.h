#pragma once

#include "esp_heap_caps.h"
#include <cstddef>

namespace mp4 {

inline void *psram_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

inline void *psram_realloc(void *ptr, size_t size) {
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
}

inline void psram_free(void *ptr) {
    heap_caps_free(ptr);
}

template <typename T>
T *psram_alloc(size_t count) {
    return static_cast<T *>(heap_caps_malloc(count * sizeof(T), MALLOC_CAP_SPIRAM));
}

inline void *internal_malloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
}

inline void safe_free(void *ptr) {
    if (ptr) heap_caps_free(ptr);
}

}  // namespace mp4
