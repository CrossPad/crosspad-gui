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
)

# The fonts in src/fonts/ replace LVGL's own (cmake/crosspad_gui_fonts.cmake).
# This archive is scanned before LVGL's, and nothing here may be what first
# asks for a given size -- LVGL's default theme, the apps and the firmware
# reference them too -- so each enabled size is forced undefined from the
# start of the link and taken from this component.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/crosspad_gui_fonts.cmake)
idf_component_get_property(_lvgl_lib lvgl__lvgl COMPONENT_LIB)
crosspad_gui_replace_lvgl_fonts(${_lvgl_lib})
crosspad_gui_font_sizes(_font_sizes)
foreach(_size ${_font_sizes})
    if(CONFIG_LV_FONT_MONTSERRAT_${_size})
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_font_montserrat_${_size}")
    endif()
endforeach()
