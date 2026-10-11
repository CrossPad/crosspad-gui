#pragma once
/**
 * @file kit_loader.h
 * @brief A kit loading, in the context row (UX 2000, variant D).
 *
 * A 312 x 22 bar at x 4, y 34 -- over the context row of whatever is on
 * screen, under the transport bar, which stays visible. A mint fill grows
 * left to right and the text flips to ink where the fill is under it:
 * "LOADING <kit>  38%", then "KIT LOADED  16/16", which fades out after
 * 600 ms. An error is the same bar in red and stays until it is tapped or the
 * next load starts.
 *
 * It replaces the status bar's arc, which the transport bar hides. On the
 * overlay layer, so a screen rebuild does not take it away. Built on first
 * use. LVGL task only.
 */
#ifdef USE_LVGL
#include "lvgl.h"
#include <cstdint>

namespace crosspad_gui {

/// Show for a kit: "LOADING <name>", 0 %.
void kit_loader_start(const char* kitName);

/// 0..100. Ignored while the bar is not up.
void kit_loader_set(uint8_t pct);

/// Full mint, "KIT LOADED  <loaded>/<total>", then fades out.
void kit_loader_done(uint8_t loaded, uint8_t total);

/// Red, with @p msg; stays until tapped or the next start.
void kit_loader_error(const char* msg);

void kit_loader_hide();

} // namespace crosspad_gui

#endif // USE_LVGL
