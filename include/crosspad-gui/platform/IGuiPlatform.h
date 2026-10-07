#pragma once

#include <cstdint>
#include "lvgl.h"
#include "crosspad/stm32/Stm32ConfigSync.hpp"  // crosspad::Stm32PadLive

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

/* What the sample engine is holding right now, for the status bar.
 *
 * Two ceilings, and the smaller one binds: a resident voice is arithmetic, a
 * streaming one also has to be fed off the card and the card gives out first.
 * Measured on a v2, 44.1 kHz stereo: sixteen resident voices fit a 1451 us
 * block at 85 %, ten streaming ones at 78 %, and from twelve streaming the
 * engine cannot feed them all. The engine fades its oldest streaming voice
 * rather than letting several starve, so what the meter warns about is a
 * *tail being cut*, never a hit being lost.
 *
 * `limit` is already the one that applies to what is playing, so the bar can
 * print "active/limit" without knowing any of the above. */
struct VoiceMeter {
    uint8_t active    = 0;   // voices sounding
    uint8_t streaming = 0;   // of those, how many are reading the card
    uint8_t limit     = 0;   // 0 when no engine holds a kit: nothing to show
};

// What the USB Audio host records, and where each codec sends its output.
// Codes match the firmware's audio-route wire format (see the ESP-IDF
// audio_route_control.h): ADC 0=DIFF 1=LINE1 2=LINE2, DAC 1=LINE1 2=LINE2 3=ALL.
struct AudioRouteState {
    uint8_t captureCodec;   // codec feeding the USB mic endpoint (0|1)
    uint8_t adcInput[2];    // ADC source per codec
    uint8_t dacOutput[2];   // DAC destination per codec
    uint8_t mute[2];        // 0|1 per codec
    uint8_t preset;         // index into the platform preset table, or kAudioRoutePresetCustom
};

// Reported (and stored) when the routing matches no preset.
static constexpr uint8_t kAudioRoutePresetCustom = 0x7F;

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

    /// Voices sounding and the ceiling that applies. Polled by the status
    /// bar; a platform with no sample engine leaves `limit` at 0 and nothing
    /// is drawn.
    virtual VoiceMeter getVoiceMeter() { return {}; }

    // Called when the user interacts with the touchscreen.
    // Platform can trigger haptic feedback, reset inactivity timers, etc.
    virtual void onUserInteraction(InteractionEvent /*event*/) {}

    // Apply LCD backlight brightness (0..100). Default no-op for platforms
    // without backlight control.
    virtual void setLcdBrightness(uint8_t /*pct*/) {}

    /* The two output volumes (0..100 codec range; callers may pass 0..127 --
     * implementation should clamp). An output is a whole codec, not an output
     * pair of one, and the names below are older than the wiring they ended
     * up describing -- on CrossPad hardware:
     *
     *   setSpeakerVolume()   -> codec 0 -> the jack marked **Out 2**, the
     *                           phones, and the internal speakers beside
     *                           them: one level for both, which is why the
     *                           speakers' own control is a gate, not a fader.
     *   setHeadphoneVolume() -> codec 1 -> the jack marked **Out 1**, the
     *                           line out.
     *
     * Read the names as "output A" and "output B" and the mapping from the
     * platform (IdfGuiPlatform). Renaming them is a MAJOR bump and a change
     * in crosspad-pc too, so it waits for a release that can carry one. */
    virtual void setSpeakerVolume(uint8_t /*volume*/) {}
    virtual void setHeadphoneVolume(uint8_t /*volume*/) {}

    // USB device profile. Boards that can re-enumerate between a data profile
    // (CDC+MIDI) and a USB Audio profile (UAC2) override these; the settings UI
    // hides the row where usbAudioSupported() is false.
    virtual bool usbAudioSupported() { return false; }
    virtual void setUsbAudioEnabled(bool /*enabled*/) {}

    // True when writeStmConfig*() actually reaches the STM32 (rev2 I2C control
    // plane). The settings UI hides the STM-backed panels when this is false so
    // no board shows sliders that cannot take effect.
    virtual bool hasStmConfigWrite() { return false; }

    // Write one STM config register (ESP boards only; no-op elsewhere).
    virtual void writeStmConfig8(uint16_t /*reg*/, uint8_t /*val*/) {}
    virtual void writeStmConfig16(uint16_t /*hiReg*/, uint16_t /*val*/) {}

    // Write one pad's full 6-byte STM analog config block (ESP boards only; no-op elsewhere).
    virtual void writeStmConfigPad(uint8_t /*padIdx*/) {}

    // Read one pad's last note-on pre-curve linear value (0..127). Returns false
    // off-ESP / on bus error (caller shows "—"). ESP boards only.
    virtual bool readStmPadVelRaw(uint8_t /*padIdx*/, uint8_t& /*out*/) { return false; }

    // Read one pad's combined live state (pressure/pan/last-velraw + raw A/B). False off-ESP.
    virtual bool readStmPadLive(uint8_t /*padIdx*/, crosspad::Stm32PadLive& /*out*/) { return false; }

    // Live codec routing behind the USB Audio profile. Boards without a
    // switchable capture path leave audioRouteSupported() false and the
    // settings UI omits the whole section, so no board shows a preset it
    // cannot honour. getAudioRoute() returns false when unsupported.
    // Appended at the end deliberately: inserting virtuals mid-class renumbers
    // every slot after them, and any translation unit compiled against an
    // older copy of this header then calls the wrong one.
    virtual bool audioRouteSupported() { return false; }
    virtual bool getAudioRoute(AudioRouteState & /*out*/) { return false; }
    // Preset index; out-of-range values (including kAudioRoutePresetCustom)
    // are ignored rather than clamped — a custom routing has no target state.
    virtual void setAudioRoutePreset(uint8_t /*preset*/) {}
    virtual void setAudioCaptureCodec(uint8_t /*codec*/) {}
    virtual void setAudioAdcInput(uint8_t /*codec*/, uint8_t /*input*/) {}
    virtual void setAudioDacOutput(uint8_t /*codec*/, uint8_t /*output*/) {}
    virtual void setAudioCodecMute(uint8_t /*codec*/, bool /*mute*/) {}
    /// Gain on the bus the USB mic endpoint carries, in dB above unity.
    /// Separate from the codec volumes on purpose: it changes what a DAW
    /// records without changing what comes out of the speakers.
    virtual void setDawOutputGain(uint8_t /*db*/) {}

    // Hands the SD card to the USB host as a mass-storage drive. It costs a
    // restart and stops audio, so the USB page confirms before calling
    // requestUsbTransfer(); boards without the mode leave this false and the
    // row is not drawn.
    virtual bool usbTransferSupported() { return false; }
    virtual void requestUsbTransfer() {}

    // What a Settings factory reset must clear beyond the settings: state the
    // platform keeps on its own, such as saved WiFi networks (and a radio the
    // reset has just switched off). Called on the LVGL thread after the reset
    // settings are saved, so it must not block or write flash there itself.
    virtual void onFactoryReset() {}
};

// Set the platform implementation (call once at startup)
void setGuiPlatform(IGuiPlatform * platform);

// Get the platform implementation
IGuiPlatform & getGuiPlatform();

} // namespace crosspad_gui
