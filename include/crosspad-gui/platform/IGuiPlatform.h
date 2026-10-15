// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include "lvgl.h"
#include "crosspad/stm32/Stm32ConfigSync.hpp"  // crosspad::Stm32PadLive
#include "crosspad/settings/CrosspadSettings.hpp" // crosspad::TouchSensitivity

namespace crosspad_gui {

/// Free and total heap memory, in bytes.
struct HeapStats {
    uint32_t sram_free;     ///< internal SRAM free
    uint32_t sram_total;    ///< internal SRAM total
    uint32_t psram_free;    ///< PSRAM free
    uint32_t psram_total;   ///< PSRAM total
};

/// Performance figures for the status displays.
struct PerfStats {
    uint32_t fps;         ///< LVGL frames per second
    uint32_t cpu0_pct;    ///< CPU core 0 usage 0-100
    uint32_t cpu1_pct;    ///< CPU core 1 usage 0-100
    uint32_t sram_pct;    ///< Internal SRAM usage 0-100
    uint32_t psram_pct;   ///< PSRAM usage 0-100
    uint32_t stm32_mps;   ///< STM32 messages per second
};

/* Two ceilings, and the smaller one binds: a resident voice is arithmetic, a
 * streaming one also has to be fed off the card and the card gives out first.
 * Measured on a v2, 44.1 kHz stereo: sixteen resident voices fit a 1451 us
 * block at 85 %, ten streaming ones at 78 %, and from twelve streaming the
 * engine cannot feed them all. */
/// What the sample engine is holding right now, for the VOICES readout.
///
/// `limit` is the voice ceiling that applies to what is playing, so a
/// display can print "active/limit" as it is. At the ceiling the engine
/// fades its oldest streaming voice to make room: a tail is cut short, and
/// every new hit still sounds.
struct VoiceMeter {
    uint8_t active    = 0;   ///< voices sounding
    uint8_t streaming = 0;   ///< of those, how many are reading the card
    uint8_t limit     = 0;   ///< the ceiling; 0 when no engine holds a kit (nothing to show)
};

/// What the USB Audio host records, and where each codec sends its output.
/// Codes match the firmware's audio-route wire format:
/// ADC 0=DIFF 1=LINE1 2=LINE2, DAC 1=LINE1 2=LINE2 3=ALL.
struct AudioRouteState {
    uint8_t captureCodec;   ///< codec feeding the USB mic endpoint (0|1)
    uint8_t adcInput[2];    ///< ADC source per codec
    uint8_t dacOutput[2];   ///< DAC destination per codec
    uint8_t mute[2];        ///< 0|1 per codec
    uint8_t preset;         ///< index into the platform preset table, or kAudioRoutePresetCustom
};

/// Reported (and stored) when the routing matches no preset.
static constexpr uint8_t kAudioRoutePresetCustom = 0x7F;

/// GUI interaction event types for haptic/activity feedback
enum class InteractionEvent : uint8_t {
    Pressed,
    Released,
    PressLost,
    LongPressed,
    LongPressedRepeat
};

/**
 * @brief What crosspad-gui needs from the platform it runs on.
 *
 * The firmware and the simulator each implement it and install it with
 * setGuiPlatform() at start-up; GUI code reaches it through getGuiPlatform().
 * Only the first five methods are required: every other one has a default
 * for a platform without that feature, and the settings UI hides the rows a
 * platform does not support.
 */
class IGuiPlatform {
public:
    virtual ~IGuiPlatform() = default;

    /// Free and total heap memory.
    virtual HeapStats getHeapStats() = 0;

    /// Milliseconds since start-up.
    virtual uint32_t millis() = 0;

    /// Prefix for asset paths (e.g. "/assets/" on ESP-IDF).
    virtual const char * assetPathPrefix() = 0;

    /// Wait @p ms milliseconds.
    virtual void delayMs(uint32_t ms) = 0;

    /// Ask the STM32 co-processor to power the board off.
    virtual void sendPowerOff() = 0;

    /// The primary navigation encoder input device (for modal group
    /// switching), or nullptr when the platform has none.
    virtual lv_indev_t * getNavigationEncoder() { return nullptr; }

    /// Performance statistics (FPS, CPU usage, memory, STM32 message rate).
    /// Default: zeros.
    virtual PerfStats getPerfStats() { return {}; }

    /// Voices sounding and the ceiling that applies. Polled by the transport
    /// bar; a platform with no sample engine leaves `limit` at 0 and nothing
    /// is drawn.
    virtual VoiceMeter getVoiceMeter() { return {}; }

    /// Called when the user interacts with the touchscreen; the platform can
    /// give haptic feedback, reset inactivity timers, etc.
    virtual void onUserInteraction(InteractionEvent /*event*/) {}

    /// Set the LCD backlight brightness, 0..100. Default: nothing.
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
    /// Volume of codec 0 -- on CrossPad the jack marked Out 2, the phones
    /// and the internal speakers -- 0..100 (larger values are clamped).
    virtual void setSpeakerVolume(uint8_t /*volume*/) {}
    /// Volume of codec 1 -- on CrossPad the jack marked Out 1, the line out
    /// -- 0..100 (larger values are clamped).
    virtual void setHeadphoneVolume(uint8_t /*volume*/) {}

    /// Whether the board can switch between its data USB profile (CDC +
    /// MIDI) and a USB Audio profile (UAC2). The settings UI hides the row
    /// when it cannot.
    virtual bool usbAudioSupported() { return false; }
    /// Switch to the USB Audio profile (true) or the data profile (false).
    virtual void setUsbAudioEnabled(bool /*enabled*/) {}

    /// True when writeStmConfig*() reaches the STM32. The settings UI hides
    /// the STM-backed panels when this is false.
    virtual bool hasStmConfigWrite() { return false; }

    /// Write one 8-bit STM config register. Default: nothing.
    virtual void writeStmConfig8(uint16_t /*reg*/, uint8_t /*val*/) {}
    /// Write one 16-bit STM config value starting at @p hiReg. Default: nothing.
    virtual void writeStmConfig16(uint16_t /*hiReg*/, uint16_t /*val*/) {}

    /// Write one pad's full 6-byte STM analog config block. Default: nothing.
    virtual void writeStmConfigPad(uint8_t /*padIdx*/) {}

    /// Read one pad's last note-on value before the velocity curve (0..127).
    /// False when the platform cannot read it (the caller shows "—").
    virtual bool readStmPadVelRaw(uint8_t /*padIdx*/, uint8_t& /*out*/) { return false; }

    /// Read one pad's live state (pressure, pan, last raw velocity, raw cell
    /// values). False when the platform cannot read it.
    virtual bool readStmPadLive(uint8_t /*padIdx*/, crosspad::Stm32PadLive& /*out*/) { return false; }

    // Appended at the end deliberately: inserting virtuals mid-class renumbers
    // every slot after them, and any translation unit compiled against an
    // older copy of this header then calls the wrong one.
    /// Whether the board has live codec routing behind the USB Audio profile.
    /// The settings UI omits the routing section when it does not.
    virtual bool audioRouteSupported() { return false; }
    /// Read the current routing; false when routing is not supported.
    virtual bool getAudioRoute(AudioRouteState & /*out*/) { return false; }
    /// Apply routing preset @p preset. Out-of-range values (including
    /// kAudioRoutePresetCustom) are ignored rather than clamped: a custom
    /// routing has no target state.
    virtual void setAudioRoutePreset(uint8_t /*preset*/) {}
    /// Choose the codec (0|1) that feeds the USB mic endpoint.
    virtual void setAudioCaptureCodec(uint8_t /*codec*/) {}
    /// Set @p codec's ADC source (see AudioRouteState for the codes).
    virtual void setAudioAdcInput(uint8_t /*codec*/, uint8_t /*input*/) {}
    /// Set @p codec's DAC destination (see AudioRouteState for the codes).
    virtual void setAudioDacOutput(uint8_t /*codec*/, uint8_t /*output*/) {}
    /// Mute or unmute @p codec.
    virtual void setAudioCodecMute(uint8_t /*codec*/, bool /*mute*/) {}
    /// Gain on the bus the USB mic endpoint carries, in dB above unity.
    /// Separate from the codec volumes on purpose: it changes what a DAW
    /// records without changing what comes out of the speakers.
    virtual void setDawOutputGain(uint8_t /*db*/) {}

    /// Whether the board can hand its SD card to the USB host as a
    /// mass-storage drive. The row is not drawn when it cannot.
    virtual bool usbTransferSupported() { return false; }
    /// Hand the SD card to the USB host. It costs a restart and stops audio,
    /// so the USB page asks the player to confirm before calling it.
    virtual void requestUsbTransfer() {}

    /// What a Settings factory reset must clear beyond the settings: state
    /// the platform keeps on its own, such as saved WiFi networks (and a
    /// radio the reset has just switched off). Called on the LVGL thread
    /// after the reset settings are saved, so it must not block or write
    /// flash there itself.
    virtual void onFactoryReset() {}

    /// Set the touch controller's threshold for a sensitivity level; applied
    /// at once, no reboot. Called on the LVGL thread, the same one that reads
    /// touch.
    virtual void setTouchSensitivity(crosspad::TouchSensitivity /*level*/) {}

    /// Run @p cb on the LVGL task; safe from any task. Use it for GUI work
    /// from a pad callback or an event-bus handler: it never waits for the
    /// display lock, so it cannot block on the LVGL thread.
    ///
    /// The default calls lv_async_call(), which is safe only on the LVGL task
    /// (or under the display lock) when LVGL is built without an OS layer.
    /// Platforms with a display lock override this to hand the call to a
    /// task that takes the lock.
    virtual void postToGui(lv_async_cb_t cb, void * user_data) { lv_async_call(cb, user_data); }

    /// Where @p path, as the file explorer is handed it (a card path such as
    /// /crosspad/kits), is on crosspad::getFileSystem(); the path itself by
    /// default. The explorer's rows keep the path it was handed.
    virtual std::string storagePath(const std::string & path) { return path; }
};

/// Install the platform implementation (call once at start-up).
void setGuiPlatform(IGuiPlatform * platform);

/// The platform implementation installed with setGuiPlatform().
IGuiPlatform & getGuiPlatform();

} // namespace crosspad_gui
