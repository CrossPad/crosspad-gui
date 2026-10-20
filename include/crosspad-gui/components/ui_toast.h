// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

/// Toast severity: an ordinary message.
#define UI_TOAST_INFO    0
/// Toast severity: something worked.
#define UI_TOAST_SUCCESS 1
/// Toast severity: a warning.
#define UI_TOAST_WARNING 2
/// Toast severity: something failed.
#define UI_TOAST_ERROR   3

/// Show a temporary message at the bottom of the screen for @p duration_ms,
/// coloured by @p severity (UI_TOAST_*). It replaces a toast already shown.
void ui_toast_show(const char * message, uint32_t duration_ms = 2000, uint8_t severity = UI_TOAST_INFO);

/// Dismiss any active toast immediately.
void ui_toast_dismiss(void);

#endif // USE_LVGL
