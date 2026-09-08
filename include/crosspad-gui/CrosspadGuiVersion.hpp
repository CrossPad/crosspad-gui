#pragma once

/**
 * @file CrosspadGuiVersion.hpp
 * @brief Crosspad-gui library version, and the crosspad-core it was written for.
 *
 * Bump rules: MAJOR when an interface a consumer implements or calls changes
 * shape; MINOR when something is added; PATCH for fixes. The REQUIRES_CORE_*
 * triple is checked at compile time (crosspad/RequireVersion.hpp) from
 * src/CrosspadGuiVersion.cpp, so a gui checked out against the wrong core
 * fails in the gui component itself, not somewhere downstream.
 */

#include "crosspad/CrosspadCoreVersion.hpp"

#define CROSSPAD_GUI_VERSION_MAJOR 1
#define CROSSPAD_GUI_VERSION_MINOR 4
#define CROSSPAD_GUI_VERSION_PATCH 1
#define CROSSPAD_GUI_VERSION "1.4.0"

#define CROSSPAD_GUI_REQUIRES_CORE_MAJOR 1
#define CROSSPAD_GUI_REQUIRES_CORE_MINOR 6
#define CROSSPAD_GUI_REQUIRES_CORE_PATCH 1

namespace crosspad_gui {

/** Version baked into crosspad-gui's compiled objects (see crosspad::coreVersion). */
crosspad::LibVersion guiVersion();

} // namespace crosspad_gui
