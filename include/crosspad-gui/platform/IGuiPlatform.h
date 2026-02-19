#pragma once

#include <cstdint>

namespace crosspad_gui {

struct HeapStats {
    uint32_t sram_free;
    uint32_t sram_total;
    uint32_t psram_free;
    uint32_t psram_total;
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
};

// Set the platform implementation (call once at startup)
void setGuiPlatform(IGuiPlatform * platform);

// Get the platform implementation
IGuiPlatform & getGuiPlatform();

} // namespace crosspad_gui
