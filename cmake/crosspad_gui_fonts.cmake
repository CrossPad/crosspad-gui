# The Montserrat sizes the UI uses come from src/fonts/, not from LVGL: same
# TTF and the same LV_SYMBOL_* list, plus Latin-1 Supplement and Latin
# Extended-A, so a kit called "ŚWIĘTE SŁOWA" draws its own letters instead of a
# row of boxes. They keep LVGL's symbol names, so no call site changes -- which
# means LVGL's own copies of those sizes have to leave the build, or the link
# ends in duplicate symbols (or, worse, picks LVGL's ASCII-only copy). Every
# platform that builds LVGL from source calls crosspad_gui_replace_lvgl_fonts()
# on its LVGL target. Regenerate the fonts with tools/fonts/generate.py; a size
# enabled in LVGL's configuration that is not generated here keeps LVGL's
# version.
#
#   crosspad_gui_font_sizes(<out_var>)           sizes in src/fonts/, e.g. 10;12;14
#   crosspad_gui_replace_lvgl_fonts(<lvgl_target>)

set(CROSSPAD_GUI_FONTS_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/fonts")

function(crosspad_gui_font_sizes out_var)
    file(GLOB _fonts "${CROSSPAD_GUI_FONTS_DIR}/lv_font_montserrat_*.c")
    set(_sizes "")
    foreach(_font ${_fonts})
        string(REGEX REPLACE ".*lv_font_montserrat_([0-9]+)\\.c$" "\\1" _size "${_font}")
        list(APPEND _sizes ${_size})
    endforeach()
    set(${out_var} ${_sizes} PARENT_SCOPE)
endfunction()

function(crosspad_gui_replace_lvgl_fonts lvgl_target)
    crosspad_gui_font_sizes(_sizes)
    if(NOT _sizes)
        message(FATAL_ERROR "crosspad-gui: no fonts in ${CROSSPAD_GUI_FONTS_DIR}")
    endif()
    string(REPLACE ";" "|" _alternatives "${_sizes}")
    get_target_property(_srcs ${lvgl_target} SOURCES)
    list(FILTER _srcs EXCLUDE REGEX "lv_font_montserrat_(${_alternatives})\\.c$")
    set_property(TARGET ${lvgl_target} PROPERTY SOURCES ${_srcs})
endfunction()
