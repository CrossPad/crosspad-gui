# Host tests for crosspad-gui widgets: LVGL 9.3 headless (a display whose flush
# does nothing) + Catch2. Build: cmake -S tests -B tests/build && cmake --build tests/build
cmake_minimum_required(VERSION 3.16)
project(crosspad-gui-tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.0)
FetchContent_MakeAvailable(Catch2)

# The firmware's LVGL (managed component lvgl/lvgl 9.3.0), compiled here from
# its sources with tests/lv_conf.h as the only configuration. SOURCE_SUBDIR
# points nowhere so LVGL's own CMake (and its configuration) is not used.
FetchContent_Declare(lvgl GIT_REPOSITORY https://github.com/lvgl/lvgl.git GIT_TAG v9.3.0 SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(lvgl)
file(GLOB_RECURSE LVGL_SOURCES ${lvgl_SOURCE_DIR}/src/*.c)
add_library(lvgl_host STATIC ${LVGL_SOURCES})
target_include_directories(lvgl_host PUBLIC ${lvgl_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lvgl_host PUBLIC LV_CONF_INCLUDE_SIMPLE)

set(GUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CORE_DIR ${GUI_DIR}/../crosspad-core CACHE PATH "crosspad-core checkout (PortableFft for the spectrum test)")
add_executable(crosspad_gui_tests
    test_ui_tile_grid.cpp
    test_spectrum_math.cpp
    test_scope_spectrum.cpp
    lvgl_headless.cpp
    png_write.cpp
    ${GUI_DIR}/src/components/ui_tile_grid.cpp
    ${GUI_DIR}/src/components/spectrum_math.cpp
    ${GUI_DIR}/src/components/scope_view.cpp
    ${CORE_DIR}/src/dsp/FftFactory.cpp
    ${CORE_DIR}/src/dsp/ScopeTrigger.cpp
    ${CORE_DIR}/src/dsp/PortableFft.cpp
)
target_include_directories(crosspad_gui_tests PRIVATE ${GUI_DIR}/include ${CORE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(crosspad_gui_tests PRIVATE USE_LVGL=1)
target_link_libraries(crosspad_gui_tests PRIVATE lvgl_host Catch2::Catch2WithMain)
# Tests make LVGL's allocations fail through this wrap (see test_ui_tile_grid.cpp).
target_link_options(crosspad_gui_tests PRIVATE -Wl,--wrap=lv_malloc_core)

include(CTest)
include(Catch)
catch_discover_tests(crosspad_gui_tests)
