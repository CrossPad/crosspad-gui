// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Severity levels for toast coloring
#define UI_TOAST_INFO    0
#define UI_TOAST_SUCCESS 1
#define UI_TOAST_WARNING 2
#define UI_TOAST_ERROR   3

// Show a temporary message at the bottom of the screen.
void ui_toast_show(const char * message, uint32_t duration_ms = 2000, uint8_t severity = UI_TOAST_INFO);

// Dismiss any active toast immediately.
void ui_toast_dismiss(void);

#endif // USE_LVGL
