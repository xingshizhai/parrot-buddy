idf_component_register(
    SRCS
        "src/ui_manager.c"
        "src/ui_screen_debug.c"
        "src/ui_screen_recording.c"
    INCLUDE_DIRS "include"
    REQUIRES
        buddy_hal
        audio_manager
        log
        lvgl__lvgl
        espressif__esp_lvgl_port
)
