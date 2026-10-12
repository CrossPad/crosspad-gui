// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file GuiServices.h
 * @brief The settings and status crosspad-gui draws, as the platform handed
 *        them to crosspad-core.
 *
 * A platform passes its CrosspadSettings and CrosspadStatus to
 * crosspad::crosspad_platform_init(), which keeps them in PlatformServices.
 * crosspad-gui reads them from there; a platform defines no globals for it.
 */

namespace crosspad {
class CrosspadSettings;
class CrosspadStatus;
}

namespace crosspad_gui {

/// PlatformServices::settings, or CrosspadSettings::getInstance() before
/// crosspad_platform_init() has run (which is also what it stores when the
/// platform passes none).
crosspad::CrosspadSettings& platformSettings();

/// PlatformServices::status, or a blank status that nothing updates before
/// crosspad_platform_init() has run.
crosspad::CrosspadStatus& platformStatus();

} // namespace crosspad_gui
