#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/platform/GuiServices.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad/platform/PlatformServices.hpp"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/status/CrosspadStatus.hpp"
#include <cassert>

namespace crosspad_gui {

static IGuiPlatform * s_guiPlatform = nullptr;
static lv_obj_t * s_overlayParent = nullptr;

void setGuiPlatform(IGuiPlatform * platform) {
    s_guiPlatform = platform;
}

IGuiPlatform & getGuiPlatform() {
    assert(s_guiPlatform != nullptr && "GUI platform not initialized! Call setGuiPlatform() first.");
    return *s_guiPlatform;
}

void setOverlayParent(lv_obj_t * parent) {
    s_overlayParent = parent;
}

lv_obj_t * getOverlayParent() {
    return s_overlayParent ? s_overlayParent : lv_layer_top();
}

crosspad::CrosspadSettings & platformSettings() {
    crosspad::CrosspadSettings * s = crosspad::getPlatformServices().settings;
    return s ? *s : *crosspad::CrosspadSettings::getInstance();
}

crosspad::CrosspadStatus & platformStatus() {
    static crosspad::CrosspadStatus s_beforeInit;
    crosspad::CrosspadStatus * s = crosspad::getPlatformServices().status;
    return s ? *s : s_beforeInit;
}

} // namespace crosspad_gui
