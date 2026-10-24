// SPDX-License-Identifier: GPL-3.0-or-later
#include "crosspad-gui/components/stm_pad_editor_logic.h"
#include "crosspad/pad/PadManager.hpp"
#include "crosspad/led/RgbColor.hpp"

namespace crosspad_gui {

static const crosspad::RgbColor kSel(0, 120, 0);     // selected = bright green
static const crosspad::RgbColor kDim(8, 8, 8);       // others = dim gray

void StmPadEditorLogic::paintSelection(crosspad::PadManager& pm) {
    for (uint8_t i = 0; i < 16; ++i)
        pm.setPadColor(i, i == selected_ ? kSel : kDim);
}

void StmPadEditorLogic::onActivate(crosspad::PadManager& pm) {
    paintSelection(pm);
}

void StmPadEditorLogic::onDeactivate(crosspad::PadManager& pm) {
    (void)pm;
    // Colors are restored by the screen's DELETE handler (which knows the
    // home/default palette); nothing to do here.
}

void StmPadEditorLogic::onPadPress(crosspad::PadManager& pm, uint8_t padIdx, uint8_t /*velocity*/) {
    if (padIdx >= 16) return;
    selected_ = padIdx;
    paintSelection(pm);
    if (selectCb_) selectCb_(selected_, selectCtx_);
}

void StmPadEditorLogic::onPadRelease(crosspad::PadManager& pm, uint8_t padIdx) {
    if (padIdx == selected_) pm.setPadColor(padIdx, kSel);  // back to full highlight
}

void StmPadEditorLogic::onPadPressure(crosspad::PadManager& pm, uint8_t padIdx, uint8_t pressure) {
    if (padIdx != selected_) return;
    // Modulate the selected pad's green by live pressure (0..127) for test feedback.
    uint8_t g = (uint8_t)(40 + ((uint16_t)pressure * (200 - 40)) / 127);
    pm.setPadColor(padIdx, crosspad::RgbColor(0, g, 0));
}

} // namespace crosspad_gui
