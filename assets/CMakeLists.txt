idf_component_register(
    SRCS
        # Add generated image .c files here as they are created:
        # "images/avatar_idle.c"
        # "images/avatar_busy.c"
        # ...
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
