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

// App base class (CRTP)
#include "crosspad-gui/app/AppBase.hpp"

// Components
#include "crosspad-gui/components/main_screen.h"
#include "crosspad-gui/components/dfu_panel.h"
#include "crosspad-gui/components/ui_modal.h"
#include "crosspad-gui/components/ui_toast.h"
#include "crosspad-gui/components/vu_meter.h"
#include "crosspad-gui/components/radial_menu.h"
#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/components/file_explorer.h"
#include "crosspad-gui/components/app_launcher.h"
#include "crosspad-gui/components/app_lifecycle.h"

// Platform
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/platform/IFileSystem.h"

// Bootstrap
#include "crosspad-gui/bootstrap.h"
