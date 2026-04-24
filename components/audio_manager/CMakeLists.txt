idf_component_register(
    SRCS "src/audio_manager.c" "src/audio_recording.c"
    INCLUDE_DIRS "include"
    REQUIRES
        buddy_hal
        log
        freertos
)
