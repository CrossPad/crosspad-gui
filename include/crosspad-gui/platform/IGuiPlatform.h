#pragma once

#include <cstdint>

// Forward-declare LVGL input device tag. Do NOT create a typedef here
// to avoid conflicts with LVGL's own typedefs in `lv_types.h`.
struct lv_indev_t;

namespace crosspad_gui {

struct HeapStats {
    uint32_t sram_free;
    uint32_t sram_total;
    uint32_t psram_free;
    uint32_t psram_total;
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
    virtual struct lv_indev_t * getNavigationEncoder() { return nullptr; }

    // Called when the user interacts with the touchscreen.
    // Platform can trigger haptic feedback, reset inactivity timers, etc.
    virtual void onUserInteraction(InteractionEvent /*event*/) {}
};

// Set the platform implementation (call once at startup)
void setGuiPlatform(IGuiPlatform * platform);

// Get the platform implementation
IGuiPlatform & getGuiPlatform();

} // namespace crosspad_gui
