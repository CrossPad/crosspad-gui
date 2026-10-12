#pragma once
/**
 * @file mode_overlay.h
 * @brief MODE + pad: switch apps the way the MPC 2000 does (UX 2000).
 *
 * The MODE button on the transport bar opens a 4×4 map of the pads under the
 * bar. Hitting a pad -- or tapping its cell -- opens that app, flips that
 * switch, and closes the map. The pads light in each entry's colour while
 * the map is open and go back to the app's own colours when it closes.
 *
 * The transport bar stays live above the map: you can stop, record or start
 * the loop while choosing where to go next.
 *
 * The map is also home. The board opens on it, and closing the last app on
 * screen brings it up: with nothing under it, MODE and the power button's
 * click leave it where it is.
 *
 * Pad numbering is the Sampler's: pad 1 bottom-left, pad 16 top-right
 * (PadManager index = pad - 1).
 *
 * All functions except mode_overlay_select_pad() and mode_overlay_is_open()
 * must be called from the LVGL task.
 */
#ifdef USE_LVGL
#include "lvgl.h"
#include <cstddef>
#include <cstdint>

namespace crosspad_gui {

enum class ModeKind : uint8_t {
    Core,    ///< Where the music is made (pads 1-4). Theme colour.
    App,     ///< An app with its own screen.
    Toggle,  ///< No screen: flips a switch and the map closes (outlined cell).
    Rare,    ///< Once-a-session apps, drawn quieter.
};

struct ModeEntry {
    uint8_t     pad;        ///< 1..16
    const char* label;      ///< On the cell, <= 8 chars, Title Case
    const char* hint;       ///< Bottom line, <= 8 chars, may be ""
    uint32_t    color;      ///< 0xRRGGBB -- cell accent and pad LED
    ModeKind    kind;
    const char* app;        ///< App name as registered (IApp::getName()); nullptr for toggles
    void (*toggle)();       ///< Called for ModeKind::Toggle
    bool (*isOn)();         ///< Optional: a toggle's state, to fill its cell
};

/// The UX 2000 map: Sampler / Sequencer / Song / Arrange, Recorder / Slicer
/// (pads 7-8 empty), PitchPlay / PadMixer / Mixer / DawControl, KitSelector /
/// Instructions / Firmware / Settings. A platform that wants switches (FULL
/// LEVEL, NOTE REPEAT...) on a row passes its own table with ModeKind::Toggle
/// entries.
const ModeEntry* mode_overlay_default_map(size_t* count);

/// Where apps are started (MainScreenResult::app_container) and what the map
/// shows. @p entries are copied, so a stack table is fine. Registers the
/// "ModeOverlay" pad logic, installs mode_overlay_toggle() as the transport
/// bar's MODE handler, and has the orchestrator close the map before any app
/// starts, whoever started it.
void mode_overlay_init(lv_obj_t* app_container, const ModeEntry* entries, size_t count);

/// Bind a switch's action and state to the entry with that pad number.
/// Works on the default map (it keeps its own mutable copy).
void mode_overlay_bind_toggle(uint8_t pad, void (*toggle)(), bool (*isOn)());

void mode_overlay_open();
void mode_overlay_close();
void mode_overlay_toggle();
bool mode_overlay_is_open();

/// Act on pad @p padIdx (0..15) as if its cell were tapped. Safe from any
/// task: the work is posted with IGuiPlatform::postToGui().
void mode_overlay_select_pad(uint8_t padIdx);

/// Go home -- what the board opens on: every app on screen is closed and the
/// map comes up with nothing under it. Whatever holds the transport keeps
/// playing.
void mode_overlay_go_home();

/// Open the app registered under @p name as if its pad had been hit: the same
/// container, the same hand-over of the pads, the same transport-bar refresh.
/// Returns false when no app answers to that name, or when it is already the
/// one running.
///
/// This is how one app hands the screen to another -- Arrange opening the
/// Recorder to fill a new audio track, a seq track's EDIT opening the Sampler
/// -- instead of each app learning the launch rules for itself.
bool mode_overlay_open_app(const char* name);

} // namespace crosspad_gui

#endif // USE_LVGL
