idf_component_register(
    SRCS "src/agent_core.c"
    INCLUDE_DIRS "include"
    REQUIRES
        log
        freertos
)
