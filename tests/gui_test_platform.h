// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// What a crosspad-gui test needs from a platform, and nothing more: LVGL
/// headless, an encoder input device, an IGuiPlatform, and crosspad-core
/// initialised over fakes (synchronous event bus, a clock the test moves).

#include "lvgl.h"

#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad/event/SyncEventBus.hpp"
#include "crosspad/led/ILedStrip.hpp"
#include "crosspad/midi/NullMidiOutput.hpp"
#include "crosspad/pad/PadAnimator.hpp"
#include "crosspad/pad/PadLedController.hpp"
#include "crosspad/pad/PadManager.hpp"
#include "crosspad/platform/IClock.hpp"
#include "crosspad/status/CrosspadStatus.hpp"

namespace guitest {

class TestClock : public crosspad::IClock {
public:
    int64_t getTimeUs() override { return timeUs; }
    int64_t timeUs = 0;
};

class NullLedStrip : public crosspad::ILedStrip {
public:
    void begin() override {}
    void setPixel(uint16_t, crosspad::RgbColor) override {}
    void refresh() override {}
    uint16_t getPixelCount() const override { return 16; }
};

class TestGuiPlatform : public crosspad_gui::IGuiPlatform {
public:
    crosspad_gui::HeapStats getHeapStats() override { return {}; }
    uint32_t millis() override { return lv_tick_get(); }
    const char* assetPathPrefix() override { return "A:"; }
    void delayMs(uint32_t) override {}
    void sendPowerOff() override {}
    lv_indev_t* getNavigationEncoder() override { return encoder; }

    lv_indev_t* encoder = nullptr;
};

/// One per test case: a fresh screen with an encoder on its own group, the
/// gui platform registered, crosspad-core initialised. Torn down again on
/// destruction, so test cases do not see each other's state.
struct GuiFixture {
    TestGuiPlatform gui;
    TestClock clock;
    NullLedStrip ledStrip;
    crosspad::SyncEventBus eventBus;
    crosspad::NullMidiOutput midiOutput;
    crosspad::PadManager padManager;
    crosspad::PadLedController padLedController;
    crosspad::PadAnimator padAnimator;
    crosspad::CrosspadStatus status;

    lv_obj_t* screen = nullptr;
    lv_group_t* group = nullptr;

    GuiFixture();
    ~GuiFixture();

    /// Let @p ms of LVGL time pass and run whatever became due.
    void advance(uint32_t ms);
};

} // namespace guitest
