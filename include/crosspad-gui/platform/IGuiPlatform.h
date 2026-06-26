#pragma once

#include <cstdint>
#include "lvgl.h"

namespace crosspad_gui {

struct HeapStats {
    uint32_t sram_free;
    uint32_t sram_total;
    uint32_t psram_free;
    uint32_t psram_total;
};

struct PerfStats {
    uint32_t fps;         // LVGL frames per second
    uint32_t cpu0_pct;    // CPU core 0 usage 0-100
    uint32_t cpu1_pct;    // CPU core 1 usage 0-100
    uint32_t sram_pct;    // Internal SRAM usage 0-100
    uint32_t psram_pct;   // PSRAM usage 0-100
    uint32_t stm32_mps;   // STM32 messages per second
};

// GUI interaction event types for haptic/activity feedback
enum class InteractionEvent : uint8_t {
    Pressed,
    Released,
    PressLost,
    LongPressed,
    LongPressedRepeat
};

class IGuiPlatform {
public:
    virtual ~IGuiPlatform() = default;

    // Get free heap memory stats
    virtual HeapStats getHeapStats() = 0;

    // Get current time in milliseconds
    virtual uint32_t millis() = 0;

    // Get asset path prefix (e.g. "I:/" for Arduino, "/assets/" for IDF)
    virtual const char * assetPathPrefix() = 0;

    // Delay for the given number of milliseconds (platform-specific)
    virtual void delayMs(uint32_t ms) = 0;

    // Send power-off command to the STM32 co-processor
    virtual void sendPowerOff() = 0;

    // Get the primary navigation encoder input device (for modal group switching).
    // Returns nullptr if no encoder is available on this platform.
    virtual lv_indev_t * getNavigationEncoder() { return nullptr; }

    // Get performance statistics (FPS, CPU usage, memory, STM32 message rate).
    // Default returns zeros — override on platforms that can provide real data.
    virtual PerfStats getPerfStats() { return {}; }

    // Called when the user interacts with the touchscreen.
    // Platform can trigger haptic feedback, reset inactivity timers, etc.
    virtual void onUserInteraction(InteractionEvent /*event*/) {}

    // Apply LCD backlight brightness (0..100). Default no-op for platforms
    // without backlight control.
    virtual void setLcdBrightness(uint8_t /*pct*/) {}

    // Apply speaker codec output volume (0..100 codec range; callers may pass
    // 0..127 — implementation should clamp).
    virtual void setSpeakerVolume(uint8_t /*volume*/) {}

    // Apply headphone codec output volume (0..100 codec range; same caveat).
    virtual void setHeadphoneVolume(uint8_t /*volume*/) {}

    // Write one STM config register (ESP boards only; no-op elsewhere).
    virtual void writeStmConfig8(uint16_t /*reg*/, uint8_t /*val*/) {}
    virtual void writeStmConfig16(uint16_t /*hiReg*/, uint16_t /*val*/) {}

    // Write one pad's full 6-byte STM analog config block (ESP boards only; no-op elsewhere).
    virtual void writeStmConfigPad(uint8_t /*padIdx*/) {}
};

// Set the platform implementation (call once at startup)
void setGuiPlatform(IGuiPlatform * platform);

// Get the platform implementation
IGuiPlatform & getGuiPlatform();

} // namespace crosspad_gui
