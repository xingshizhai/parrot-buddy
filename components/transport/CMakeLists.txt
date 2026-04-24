idf_component_register(
    SRCS
        "src/transport.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        log
        lwip
)
