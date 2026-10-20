// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// CrossPad GUI - Shared LVGL components
// Include this header for access to all shared GUI components.

// Theme
#include "crosspad-gui/theme/crosspad_theme.h"

// Styles
#include "crosspad-gui/styles/styles.h"

// Widgets
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad-gui/widgets/keypad_buttons.h"
#include "crosspad-gui/widgets/footer_button.h"
#include "crosspad-gui/widgets/spinbox_helpers.h"
#include "crosspad-gui/widgets/settings_widgets.h"

// App base class (CRTP) and non-template interface
#include "crosspad-gui/app/ILvglApp.h"
#include "crosspad-gui/app/AppBase.hpp"

// Components
#include "crosspad-gui/components/main_screen.h"
#include "crosspad-gui/components/dfu_panel.h"
#include "crosspad-gui/components/ui_modal.h"
#include "crosspad-gui/components/ui_toast.h"
#include "crosspad-gui/components/vu_meter.h"
#include "crosspad-gui/components/segment_meter.h"
#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/components/file_explorer.h"
#include "crosspad-gui/components/app_launcher.h"
#include "crosspad-gui/components/app_lifecycle.h"
#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/components/power_gesture.h"

// Platform
#include "crosspad-gui/platform/IGuiPlatform.h"

// Virtual Device (software-rendered hardware controls)
#include "crosspad-gui/virtual_device/VirtualPadGrid.h"
#include "crosspad-gui/virtual_device/VirtualEncoder.h"
#include "crosspad-gui/virtual_device/VirtualPowerButton.h"

// Bootstrap
#include "crosspad-gui/bootstrap.h"
