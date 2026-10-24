// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file markdown_view.h
 * @brief Simple markdown renderer for LVGL
 *
 * Renders basic markdown (headings, bullets, tables, text) into an LVGL
 * flex-column container. Supports parsing from string or file.
 */

#include "lvgl.h"
#include <string>

namespace crosspad_gui {

/// Render markdown text into an LVGL parent container (flex column).
/// Clears existing children before rendering.
void markdown_render(lv_obj_t* parent, const std::string& markdown);

/// Render markdown from a file path.
void markdown_render_file(lv_obj_t* parent, const std::string& filepath);

} // namespace crosspad_gui
