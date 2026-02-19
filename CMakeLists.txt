# CrossPad GUI — ESP-IDF Component CMakeLists.txt
#
# Shared LVGL GUI components for CrossPad projects.
# Used as an ESP-IDF component via git submodule.

file(GLOB_RECURSE GUI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

idf_component_register(
    SRCS ${GUI_SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    REQUIRES
        lvgl
        crosspad-core
        freertos
)
