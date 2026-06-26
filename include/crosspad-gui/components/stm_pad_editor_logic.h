#pragma once
#include <cstdint>
#include "crosspad/pad/IPadLogicHandler.hpp"

namespace crosspad_gui {

class StmPadEditorLogic : public crosspad::IPadLogicHandler {
public:
    using PadSelectCb = void(*)(uint8_t padIdx, void* ctx);
    void setSelectCallback(PadSelectCb cb, void* ctx) { selectCb_ = cb; selectCtx_ = ctx; }
    uint8_t selectedPad() const { return selected_; }

    void onActivate(crosspad::PadManager& pm) override;
    void onDeactivate(crosspad::PadManager& pm) override;
    void onPadPress(crosspad::PadManager& pm, uint8_t padIdx, uint8_t velocity) override;
    void onPadRelease(crosspad::PadManager& pm, uint8_t padIdx) override;
    void onPadPressure(crosspad::PadManager& pm, uint8_t padIdx, uint8_t pressure) override;

private:
    void paintSelection(crosspad::PadManager& pm);
    uint8_t selected_ = 0;
    PadSelectCb selectCb_ = nullptr;
    void* selectCtx_ = nullptr;
};

} // namespace crosspad_gui
