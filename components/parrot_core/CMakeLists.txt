idf_component_register(
    SRCS "src/parrot_core.c" "src/sound_detection.c"
    INCLUDE_DIRS "include"
    REQUIRES
        audio_manager
        agent_core
        esp_timer
        freertos
        log
)
