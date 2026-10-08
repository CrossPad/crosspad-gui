# CrossPad GUI — ESP-IDF Component CMakeLists.txt
#
# Shared LVGL GUI components for CrossPad projects.
# Used as an ESP-IDF component via git submodule.

# .c as well as .cpp: the design language (src/design) arrived as a generator's
# C output -- bitmap fonts and 1-bit icons -- and is kept in that form so a
# re-export is a copy rather than a rewrite.
file(GLOB_RECURSE GUI_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

idf_component_register(
    SRCS ${GUI_SOURCES}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    REQUIRES
        lvgl
        crosspad-core
        freertos
)
