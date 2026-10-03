#pragma once
#include "lvgl.h"

/// lv_init() and a 320x240 display that draws into a frame buffer nobody sees
/// unless lvgl_headless_write_png() is asked for it. Idempotent.
void lvgl_headless_init();

/// Renders the active screen and writes the 320x240 frame as a PNG.
bool lvgl_headless_write_png(const char* path);
