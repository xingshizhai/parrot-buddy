cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED SMART_BUDDY_BOARD)
    set(SMART_BUDDY_BOARD "esp32s3_box3")
endif()

set(SDKCONFIG_DEFAULTS
    "sdkconfig.defaults"
    "sdkconfig.defaults.esp32s3"
)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/components"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/boards/${SMART_BUDDY_BOARD}"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart_buddy)
