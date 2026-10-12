// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file psram_placement.h
 * @brief CROSSPAD_GUI_PSRAM_BSS: static state no frame reads in a hurry (a
 *        view's bookkeeping, dialog text) placed in external RAM where the
 *        platform has it, so internal RAM stays for what needs it.
 *
 * The ESP-IDF build (ESP_PLATFORM) defines it to esp_attr.h's
 * EXT_RAM_BSS_ATTR; everywhere else it is empty and the variable is ordinary
 * .bss. Zero-initialised either way.
 */

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define CROSSPAD_GUI_PSRAM_BSS EXT_RAM_BSS_ATTR
#else
#define CROSSPAD_GUI_PSRAM_BSS
#endif
