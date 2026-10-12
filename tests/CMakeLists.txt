# Host tests for crosspad-gui: LVGL 9.3 headless (a display whose flush does
# nothing) + Catch2, linked against crosspad-core's portable sources.
# Build: cmake -S tests -B tests/build && cmake --build tests/build
cmake_minimum_required(VERSION 3.16)
project(crosspad-gui-tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.0)
FetchContent_MakeAvailable(Catch2)

# crosspad-core's settings headers include it.
FetchContent_Declare(ArduinoJson GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git GIT_TAG v7.3.0)
FetchContent_MakeAvailable(ArduinoJson)

# The firmware's LVGL (managed component lvgl/lvgl 9.3.0), compiled here from
# its sources with tests/lv_conf.h as the only configuration. SOURCE_SUBDIR
# points nowhere so LVGL's own CMake (and its configuration) is not used.
FetchContent_Declare(lvgl GIT_REPOSITORY https://github.com/lvgl/lvgl.git GIT_TAG v9.3.0 SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(lvgl)
file(GLOB_RECURSE LVGL_SOURCES ${lvgl_SOURCE_DIR}/src/*.c)
add_library(lvgl_host STATIC ${LVGL_SOURCES})
# src/ too: gui reaches a few private LVGL headers by their path under it.
target_include_directories(lvgl_host PUBLIC ${lvgl_SOURCE_DIR} ${lvgl_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lvgl_host PUBLIC LV_CONF_INCLUDE_SIMPLE)
# Montserrat comes from src/fonts/, as on every platform.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/crosspad_gui_fonts.cmake)
crosspad_gui_replace_lvgl_fonts(lvgl_host)

# crosspad-core beside this repository, as every platform checks it out, or
# wherever CROSSPAD_CORE_DIR points. Its FreeRTOS / ESP-IDF adapters are left
# out: crosspad-gui must link without them.
set(GUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CROSSPAD_CORE_DIR ${GUI_DIR}/../crosspad-core CACHE PATH "crosspad-core checkout")
file(GLOB_RECURSE CORE_SOURCES ${CROSSPAD_CORE_DIR}/src/*.cpp)
list(FILTER CORE_SOURCES EXCLUDE REGEX "/(EspEventBus|FreeRtosEventBus|Drv2605Driver|Stm32Manager)\\.cpp$")
add_library(crosspad_core_host STATIC ${CORE_SOURCES})
target_include_directories(crosspad_core_host PUBLIC ${CROSSPAD_CORE_DIR}/include)
target_link_libraries(crosspad_core_host PUBLIC ArduinoJson)

# Every crosspad-gui source, linked whole into the test binary: a symbol the
# library expects a platform to define fails this link.
file(GLOB_RECURSE GUI_SOURCES ${GUI_DIR}/src/*.cpp ${GUI_DIR}/src/*.c)
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(crosspad_gui_tests
    ${TEST_SOURCES}
    lvgl_headless.cpp
    gui_test_platform.cpp
    fw_fake_service.cpp
    png_write.cpp
    ${GUI_SOURCES}
)
target_include_directories(crosspad_gui_tests PRIVATE ${GUI_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(crosspad_gui_tests PRIVATE USE_LVGL=1)
target_link_libraries(crosspad_gui_tests PRIVATE lvgl_host crosspad_core_host Catch2::Catch2WithMain)
# Tests make LVGL's allocations fail through this wrap (see test_ui_tile_grid.cpp).
target_link_options(crosspad_gui_tests PRIVATE -Wl,--wrap=lv_malloc_core)

include(CTest)
include(Catch)
catch_discover_tests(crosspad_gui_tests)
