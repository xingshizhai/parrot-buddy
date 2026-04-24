idf_component_register(
    SRCS
        "src/hal.c"
        "src/hal_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES
        nvs_flash
        log
)
