file(GLOB_RECURSE app_sources "*.cpp" "*.c")

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES
        driver
        sdmmc
        fatfs
        vfs
        esp_timer
        esp_audio_codec
)
