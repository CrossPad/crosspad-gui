// SPDX-License-Identifier: GPL-3.0-or-later
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

#define CROSSPAD_GUI_VERSION_MAJOR 2
#define CROSSPAD_GUI_VERSION_MINOR 12
#define CROSSPAD_GUI_VERSION_PATCH 0

/* Built from the triple, never written out: the literal had drifted two bumps
 * behind it (Settings > Info read "1.4.0" off a 1.5.0 library) because a bump
 * touches three lines and the string was the one that got missed. */
#define CROSSPAD_VERSION_STR_(x) #x
#define CROSSPAD_VERSION_STR3_(a, b, c) \
    CROSSPAD_VERSION_STR_(a) "." CROSSPAD_VERSION_STR_(b) "." CROSSPAD_VERSION_STR_(c)
#define CROSSPAD_GUI_VERSION CROSSPAD_VERSION_STR3_( \
    CROSSPAD_GUI_VERSION_MAJOR, CROSSPAD_GUI_VERSION_MINOR, CROSSPAD_GUI_VERSION_PATCH)

#define CROSSPAD_GUI_REQUIRES_CORE_MAJOR 2
#define CROSSPAD_GUI_REQUIRES_CORE_MINOR 24
#define CROSSPAD_GUI_REQUIRES_CORE_PATCH 0

namespace crosspad_gui {

/** Version baked into crosspad-gui's compiled objects (see crosspad::coreVersion). */
crosspad::LibVersion guiVersion();

} // namespace crosspad_gui
