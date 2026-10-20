// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/**
 * @file mode_overlay.h
 * @brief MODE + pad: switch apps from the pads.
 *
 * A 4×4 map of the pads. Hitting a pad -- or tapping its cell -- opens that
 * app or flips that switch. The pads light in each entry's colour while the
 * map is the screen and go back to the app's own colours when an app is.
 *
 * It is one of the three home screens (crosspad::LauncherStyle::Pads, beside
 * the icon grid and the list; Settings -> Display -> Home screen):
 *  - Pads: the map is the home screen, built into the app container by
 *    build_main_screen() like a launcher -- where the board opens and where
 *    closing the last app lands. MODE on the transport bar brings the map up
 *    over any app as well, and MODE or the power button's click take it
 *    away again.
 *  - Grid / List: no map; MODE is the way home.
 *
 * The transport bar stays live above the map: you can stop, record or start
 * the loop while choosing where to go next.
 *
 * Pad numbering is the Sampler's: pad 1 bottom-left, pad 16 top-right
 * (PadManager index = pad - 1).
 *
 * All functions except mode_overlay_select_pad() and mode_overlay_is_open()
 * must be called from the LVGL task.
 */
#ifdef USE_LVGL
#include "lvgl.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include <cstddef>
#include <cstdint>

namespace crosspad_gui {

enum class ModeKind : uint8_t {
    Core,    ///< Where the music is made (pads 1-4). Theme colour.
    App,     ///< An app with its own screen.
    Toggle,  ///< No screen: flips a switch and the map closes (outlined cell).
    Rare,    ///< Once-a-session apps, drawn quieter.
    More,    ///< Opens the list of every app no pad names (pad 8 on the default map).
};

/// One cell of the map: the pad it sits on and what hitting it does.
struct ModeEntry {
    uint8_t     pad;        ///< 1..16
    const char* label;      ///< On the cell, <= 8 chars, Title Case
    const char* hint;       ///< Bottom line, <= 8 chars, may be ""
    uint32_t    color;      ///< 0xRRGGBB -- cell accent and pad LED
    ModeKind    kind;       ///< what the cell is, and how it is drawn
    const char* app;        ///< App name as registered (IApp::getName()); nullptr for toggles
    void (*toggle)();       ///< Called for ModeKind::Toggle
    bool (*isOn)();         ///< Optional: a toggle's state, to fill its cell
};

/// The default map, by pad: 1-4 Sampler / Sequencer / Song / Arrange,
/// 5-8 Recorder / Slicer / (free) / More, 9-12 PitchPlay / PadMixer / Mixer /
/// DawControl, 13-16 KitSelector / Instructions / Firmware / Settings.
/// Writes the number of entries to @p count. A platform that wants switches
/// (FULL LEVEL, NOTE REPEAT...) on a row passes its own table with
/// ModeKind::Toggle entries.
const ModeEntry* mode_overlay_default_map(size_t* count);

/// Where apps are started (MainScreenResult::app_container), what the map
/// shows, and what the home screen is. @p entries are copied, so a stack
/// table is fine; an entry naming an app this firmware does not have is not
/// drawn. Registers the "ModeOverlay" pad logic, installs
/// mode_overlay_toggle() as the transport bar's MODE handler, and has the
/// orchestrator close the map before any app starts, whoever started it.
/// build_main_screen() calls it on every build.
void mode_overlay_init(lv_obj_t* app_container, const ModeEntry* entries, size_t count,
                       crosspad::LauncherStyle home);

/// The map as the home screen, filling @p parent (the app container), its
/// cells joined to @p group (the main screen's encoder group). Gone with
/// @p parent's next clean. build_main_screen() calls it for the Pads style.
lv_obj_t* mode_overlay_create_home(lv_obj_t* parent, lv_group_t* group);

/// Bind a switch's action and state to the entry with that pad number.
/// Works on the default map (it keeps its own mutable copy).
void mode_overlay_bind_toggle(uint8_t pad, void (*toggle)(), bool (*isOn)());

/// Open the map over the running app. Only where the map is home, and only
/// with an app on screen: at home the map is the screen already.
void mode_overlay_open();
/// Close the map over the app; the pads go back to the app's own logic and
/// colours. The home map is not closed -- it is the screen.
void mode_overlay_close();
/// What MODE does. Map as home: open the map over the app, or close it. Grid
/// or list as home: go home.
void mode_overlay_toggle();
/// True while a map is the screen -- the home map with nothing over it, or
/// the map over an app -- and the pads choose rather than play. Safe from
/// any task.
bool mode_overlay_is_open();

/// Act on pad @p padIdx (0..15) as if its cell were tapped. Safe from any
/// task: the work is posted with IGuiPlatform::postToGui().
void mode_overlay_select_pad(uint8_t padIdx);

/// Go home -- what the board opens on: the map over an app and every app on
/// screen are closed, which leaves the home screen, whichever style it is.
/// Whatever holds the transport keeps playing. The way back
/// (AppOrchestrator::historyBack()) starts afresh, as it does for an app
/// picked on the home map.
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
