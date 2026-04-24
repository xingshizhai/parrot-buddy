idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        buddy_hal
        agent_core
        audio_manager
        parrot_core
        ui
        transport
        protocol
        nvs_flash
        log
        freertos
        esp_wifi
        esp_netif
        esp_event
)
