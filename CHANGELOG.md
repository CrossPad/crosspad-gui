# Changelog

Notable changes to crosspad-gui. The version is the triple in
`include/crosspad-gui/CrosspadGuiVersion.hpp` (bump rules there;
`library.json` carries the same number and `^` the core it requires, checked
by `scripts/check_manifest.py`). This file starts at 2.0.0; earlier releases
are described in their commits.

## 2.11.7 — 2026-11-06

- Settings → System → Power Long-Press offers 1, 1.5, 2, 2.4 and 3 s, 2.4 s
  by default (was up to 4 s, 4 s by default): the STM 1.16 hold ladder is
  40 % shorter, and it clamps the hold under its 4.2 s ship threshold.

## 2.11.6 — 2026-11-04

- The status bar shows the charging bolt and the host companion's USB glyph
  side by side. They shared one slot and the USB glyph won, so a board
  charging from a computer running the hub never showed that it charged.

## 2.11.5 — 2026-11-04

- Quick-settings drawer: the volume sliders carry the outputs' names, one
  per output, where the platform names them (`IOutputLevels`) -- on the
  CrossPad "Speakers", "Phones" and "Line out", as Settings and the Mixer.
  They read "Out 1" and "Out 2" and set Line out (the jack marked Out 3/4)
  and the speakers alone; the phones (Out 1/2) had no slider. Without
  `IOutputLevels` the two codec levels are "Phones" and "Line out", as in
  Settings, and the volume overlay says the same.
- Quick-settings drawer: the rows share the height under the bar, at most
  34 px each, so the footer (memory, the close arrow) is no longer cut off
  at the bottom of the screen; the name column is 70 px ("Speakers").

## 2.11.4 — 2026-11-03

- File explorer (the pad editor's sample browser): the encoder walks every
  row to the end of the list, then Close and Load, then the transport bar.
  Rows go up eight at a time, and the bar's keys -- which join a screen's
  group once it has a member -- sat after the first chunk, so turning reached
  the top bar a dozen rows in. `FooterLast()` now puts the footer and every
  other member behind the rows, after each chunk, keeping the focus.
- File explorer: going back lands on the folder you came from. Up a level
  (Go back..., the power button) the left pane focuses the folder just left
  and the right pane shows its contents at once -- it stood empty, and the
  focus went to the first folder. Back from the right pane focuses the
  folder whose contents it was showing, not "Go back...".
- File explorer: "Go back..." stays the first row at the top of the left
  pane. Turning up a page near the top scrolled the list past its start
  (`lv_obj_scroll_by` is unbounded) and left an empty band above it; the
  page flips are `lv_obj_scroll_by_bounded` now, at both ends.

## 2.11.3 — 2026-10-31

- Settings: the encoder works on a page a dropdown's choice rebuilt (USB
  Mode, the theme colour, Dim/Sleep). The dropdown was deleted inside its own
  `VALUE_CHANGED`, before it took the encoder out of editing, so every turn
  went to the new page as a key and only a power click left; clearing a page
  now ends editing, and USB Mode rebuilds after the event.

## 2.11.2 — 2026-10-31

- Settings → USB: what a host records is the Mixer's USB bus, set on its
  ROUTE page; "Recording Source" moved under "Routing (advanced, until
  restart)", its "Sampler" is "Mixer ROUTE", and the choice is no longer
  saved (`CrosspadSettings::usbAudioPreset` is not written).
- The outputs are named as the board's jacks are: Capture Codec "0 - Phones",
  "1 - Line out"; the fallback sliders "Phones" and "Line out".

## 2.11.1 — 2026-10-29

Paired with crosspad-core 2.24.0.

- 2.10.2 (below) together with main's 2.11.0 (`VoiceMeter.full`, an engine
  at its own ceiling).

## 2.10.2 — 2026-10-28

- The kit loader bar says `KIT LOADED  EMPTY` for a kit with no sample on any
  pad (`kit_loader_done(0, 0)`), not `0/0`.

## 2.10.1 — 2026-10-27

Paired with crosspad-core 2.24.0.

### Fixed

- A toast (`ui_toast_show()`) is never wider than the screen: a message longer
  than a line wraps, centred, where it was centred past both edges.

## 2.10.0 — 2026-10-25

Paired with crosspad-core 2.24.0.

### Added

- Settings → MIDI: "USB -> MIDI" and "ESP -> MIDI" (the STM routes them to the
  TRS OUT jack from STM 1.14) and "MIDI Out Type" (Type A / Type B,
  `Stm32Settings::midiOutType`, written to `REG_CTL_MIDI_OUT_TYPE`).

## 2.9.0 — 2026-10-24

Paired with crosspad-core 2.17.0.

### Added

- The transport bar has a DUB key of its own, between REC and PLAY: REC is
  always `transportRecord()`, DUB `transportOverdub()` (an overdub used to be
  REC pressed while playing): an open ring and a dot over "DUB". The bar
  follows "CrossPad UX2000 Touch.html" v2: the transport keys stay 38 px,
  BPM is 40 (14 px digits), POS 38 (16 px), METRO 28 and the voices and
  battery 44, the count over an 18 x 8 battery.
- `metronome_popup`: held, METRO opens the metronome's options -- volume (5 %
  a step), density 1/4 / 1/8 / 1/16, count-in off / 1 bar / 2 bars --
  heard at once, closed by Done, a tap beside the card or the power button.

## 2.8.0 — 2026-10-24

### Added

- Settings → Info ends with a **Legal** section: the copyright and the
  licence (GPL-3.0-or-later) as rows, and *No warranty* → **Read**, a dialog
  saying the software comes with no warranty, that CrossPad is not designed
  or certified for safety-critical use, and where the licence text is — the
  notices GPL-3.0 section 5(d) asks an interactive program to show. Leaving
  Settings closes the dialog.

## 2.7.8 — 2026-10-23

### Fixed

- The transport bar's tempo tile asked its font with a bare `0` for the part:
  LVGL 9.4 (the simulator) makes `lv_part_t` an enum and the build failed;
  `LV_PART_MAIN`.

## 2.7.7 — 2026-10-23

Paired with crosspad-core 2.16.0 (built and run on the board: smoke,
app_churn, song_bounce). No change in crosspad-gui itself.

## 2.7.6 — 2026-10-23

Paired with crosspad-core 2.14.0. No change in crosspad-gui itself.

## 2.7.5 — 2026-10-23

Paired with crosspad-core 2.13.0. No change in crosspad-gui itself.

## 2.7.4 — 2026-10-23

Paired with crosspad-core 2.12.0. No change in crosspad-gui itself.

## 2.7.3 — 2026-10-23

Paired with crosspad-core 2.11.0. No change in crosspad-gui itself.

## 2.7.2 — 2026-10-23

Paired with crosspad-core 2.10.1. No change in crosspad-gui itself.

## 2.7.1 — 2026-10-23

Paired with crosspad-core 2.10.0. No change in crosspad-gui itself.

## 2.7.0 — 2026-10-22

The transport bar shows the audio render's load. Requires crosspad-core 2.9.0.

### Added

- `IGuiPlatform::getDspLoad()` (`DspLoad`): the render's average and longest
  pass as a share of a block, and the passes over a block since boot.
- The VOICES tile's caption is `CPU nn%` where the platform measures it: the
  average, smoothed; an ink chip in amber from 75 % (at 85 % the engines stop
  adding voices) and red from 90 % or for a second after a late block.

## 2.6.1 — 2026-10-22

Paired with crosspad-core 2.8.0 (every seq track keeps a scene of its own).
No change in crosspad-gui itself.

## 2.6.0 — 2026-10-22

The MODE map's pads are buttons. Requires crosspad-core 2.7.0.

### Changed

- The map's pad logic is `buttonsOnly()`: only a finger reaches it and picks
  an app, and that press is not a note of the take. A pattern playing and a
  host's MIDI keep sounding through the instrument or app under the map,
  where they used to be swallowed while it was up.

## 2.5.0 — 2026-10-22

The screen's widgets for hosts.

- `crosspad_gui::ui_tree_probe()` returns the visible widgets of the active
  screen and the top layer as flat `UiNode` data (class, LCD rectangle, text,
  interactive / checked / disabled / focused); `ui_tree_json()` serialises
  them for a host tool (the web twin's GUI map). `components/ui_probe.h`,
  LVGL thread only, host-tested in `tests/test_ui_probe.cpp`.
- Nothing existing changes; no core change.

## 2.4.2 — 2026-10-22

- Paired with crosspad-core 2.6.0 (a Pitch track beside two kit tracks). No
  change of its own.

## 2.4.1 — 2026-10-21

- Paired with crosspad-core 2.5.0 (the Slice kit). No change of its own.

## 2.4.0 — 2026-10-21

The UX 2000 top bar (Steve's 1.29.0), redone on 2.x's row.

- The transport bar is 40 px of seven tiles, 36 tall ("CrossPad UX2000
  Touch.html", v2): BPM, the position under the state word (STOP, COUNT,
  PLAY, REC, DUB), MODE, REC (an overdub onto what plays, DUB in the key),
  PLAY/STOP in one key (held: stop and back to 1.1), the metronome flashing
  on the beat (longer on the one), and VOICES with the battery. Encoder
  order BPM, position, MODE, REC, PLAY, metronome, battery; the power
  button's hold jumps to PLAY. `TRANSPORT_BAR_HEIGHT` 40: the app container
  is 200 px under it.
- Kept from 2.x: the row the bar shares with the status bar as its second
  page (dragged sideways, or the VOICES + battery tile -- the last key in the
  rotation -- and the status bar's arrow), the drawer pulled from the top
  edge, a long press on MODE to the status bar, a tap on a key leaving the
  encoder's focus where it was, back from the keys to the screen's last
  control. MODE still opens 2.x's map.
- Paired with crosspad-core 2.3.0.

## 2.3.0 — 2026-10-21

The 1.x line's Hit window settings (1.29.0 there), on 2.x's split settings
files.

- Settings -> Analog / Velocity -> Hit window: the window's length (the press
  debounce, 2..15 ms, which had no control), Mode Fixed / Adaptive (STM 1.12
  ends the window once the press has peaked), adaptive minimum, peak drop and
  peak samples. Each writes its register at once; `stm_push_config` and the
  pad-tuning import carry the four new registers too.
- Paired with crosspad-core 2.3.0.

## 2.2.0 — 2026-10-20

From the UX 2000 line (Steve's 1.29.0 there), on 2.x's widgets.

- The way back: `AppOrchestrator` keeps the registered apps opened since the
  last one opened from home, at most 8 (`historyBack()`, `clearHistory()`);
  a start of one already in it drops what was opened after it. A power
  click on an app that declines it opens the app it was opened from, and
  goes home only when there is none. Launcher rows, a pick on the home map,
  `mode_overlay_go_home()` and a rebuilt main screen start it afresh.
- `PerfStats` carries headroom in kB -- internal RAM free and its low-water
  mark, PSRAM free and its largest block -- and the audio render's load
  (`dsp_pct`) and overruns. The status bar shows them in two-line columns,
  inverted near a limit, and PSRAM free / largest block always.
- The launcher list re-reads the kit's name every 500 ms: a load made under
  an app changes it.
- `LV_STATUS_BAR_HEIGHT` is 30, the transport bar's height: at 22 the app
  container moved on every swap of the two bars.
- Paired with crosspad-core 2.2.0.

### Not taken from the UX 2000 line

- The 40 px transport bar of seven tiles (BPM, position, MODE, REC/DUB,
  PLAY/STOP in one key with a hold to stop back to 1.1, the metronome
  flashing on the beat, voices and battery), and the MODE list (one row per
  pad, home, ACTIVE/ON tags). 2.x keeps its 30 px transport bar, which
  swipes to the status bar as the row's second page, and its MODE map: one
  of the three home screens (grid, list, pad map; Settings -> Display), with
  More on pad 8. The two designs replace the same widgets, and 2.x's carry
  the swipe, the home styles and their tests; the list and the tile bar are
  to be redone on top of them if they are still wanted.

## 2.1.0 — 2026-10-19

- `VirtualPadGrid::setLedSource()`: the grid can show what the LED strip
  shows (pressed, playing, a dimmed Loop pad) instead of only each pad's
  resting colour. Default unchanged.
- The top bar slides between the transport and the status icons; the
  quick-settings drawer opens on a pull from the top edge and closes on a
  push, and its backdrop closes on a tap, not on a drag (from 1.x master).
- `AppBase` sends the transport home as an app opens and closes
  (`transportGoHome()`), so REC/PLAY on the top bar never stay greyed after
  an app that claimed them.
- Back (power click) from transport keys the encoder was turned onto lands on
  the screen's last control in one click; `transportbar_toggle_focus()`
  answers false when the screen has nothing to take the focus back, and
  `powerBack()` then moves on to the app.
- The file explorer stops adding rows below 40 kB of free PSRAM and says how
  many it left out, instead of an LVGL assert.
- Paired with crosspad-core 2.1.0.

## 2.0.0 — 2026-10-18

The open-source baseline: gui is LVGL plus crosspad-core, links on its own
and asks the platform for everything else through `IGuiPlatform` and
`PlatformServices`. Requires crosspad-core 2.0.0. Contains 1.24.0 … 1.28.0
(one slider per physical output, the kit loader in the context row,
`create_segment_meter`, Settings → Audio → Buffer, the scope's SPECTRUM mode),
described in their commits.

### Removed

- The `settings`, `status` and `status_c` globals a platform had to define:
  gui reads what the platform passed to `crosspad_platform_init()` through
  `crosspad_gui::platformSettings()` / `platformStatus()`
  (`crosspad-gui/platform/GuiServices.h`), and the status bar keeps its own
  container. A platform must no longer define them.
- `radial_menu.h` (`lv_CreateRadialMenu`, `lv_DestroyRadialMenu` and their
  types).
- `App::sequencer()`, `addCLICommand()`, `sendEvent()`, `postGuiUpdate()`,
  `registerCLICommands()`. A platform must no longer implement `App`'s
  accessors (duplicate symbols).
- `styleWaveformBox` and the `styleKitBrowser*` styles: crosspad-sampler
  defines them.
- `crosspad_gui::IFileSystem`, `PosixFileSystem`, `setFileSystem()`,
  `getFileSystem()` (use crosspad-core's `IFileSystem`), the file-system
  argument of `bootstrap()` (now `bootstrap(IGuiPlatform*)`) and
  `GuiStartConfig::filesystem`.
- The old BLE MIDI panel API: `lv_CreateMidiBLE`, `lv_MidiBLE_destroy`,
  `midi_ble_get_callbacks` and the two-field `MidiBleCallbacks`.
- `crosspad-gui/components/spectrum_math.h` (`spectrum_hann()`,
  `spectrum_map_bars()`, `spectrum_bar_hz()`, `spectrum_levels()`): the
  scope's SPECTRUM arithmetic is crosspad-core's `dsp/SpectrumBars.hpp`.
- `assets/pomoc.png`, `assets/instrukcja.png`.
- `freertos` from the ESP-IDF component's REQUIRES.

### Changed
- The launcher defaults to the list; Factory Reset puts it back to the list.

- `create_segment_meter()` and `kSegmentMeterW` moved from the global namespace
  into `crosspad_gui::`, like every other gui API.
- When an app's root is deleted outside `destroyApp()` (e.g. by
  `lv_obj_clean()`), `AppBase` calls the app's destroy function with
  `nullptr`: release what the app owns, delete nothing. A destroy function
  must guard its delete: `if (root) lv_obj_delete_async(root);`.
- `AppBase::start()` with a create function that returns no root logs an
  error and leaves the app not started; a later `start()` tries again.
- `IGuiPlatform::postToGui()` must never make its caller wait: it is called
  from pad-logic callbacks and event-bus handlers the LVGL thread may be
  waiting for.
- Every app start closes the MODE map first, programmatic starts included.
- Settings → System → **Factory Reset** asks first: a dialog *Factory
  reset?* with **Cancel** (where the encoder starts) and **Reset** (red).
  Cancel, the power button's back or leaving Settings change nothing.
- Settings no longer offers rows that changed nothing: Display → Launcher
  where no launcher is built, Audio → Audio Engine, Pads & LEDs → Note Off on
  Release and Upper Row Functions, Vibration → On Error, MIDI → Send to STM,
  BLE -> MIDI and the STM32 routing switches other than MIDI -> Thru and
  MIDI -> USB, System → Wake Brightness. Their settings keys are still
  stored, backed up and pushed as before.

### Added

- `crosspad-gui/platform/GuiServices.h`: `platformSettings()`,
  `platformStatus()`.
- `App`'s accessors (`padManager()`, `ledController()`, `status()`,
  `settings()`) implemented in gui (`src/app/App.cpp`) over
  `PlatformServices`.
- `PowerButtonClassifier`: the click/hold timing of the power button for the
  hardware and the emulated button alike; `kPowerHoldMs`,
  `kPowerWakeGuardMs`, `kPowerLightCancelWindowMs`.
- Montserrat 10, 12, 14, 16, 18 and 24 with Latin-1 Supplement and Latin
  Extended-A under LVGL's own symbol names, their generator
  (`tools/fonts/generate.py`) and `cmake/crosspad_gui_fonts.cmake`
  (`crosspad_gui_replace_lvgl_fonts()`).
- `boot_splash`: the cold-boot splash, with `boot_splash_reveal()`.
- `midi_ble_settings_build()` and the new `MidiBleCallbacks`: one Settings
  BLE MIDI page for every platform; a row whose callback is null is not
  shown.
- The Firmware app (`crosspad-gui/fw/`): Library, Store and Updates views,
  kit card, actions, icons and `fw_app`, behind `IFwViewService`
  (`setFwViewService()`, `FwAppHooks`); the kit card's action table
  `FwCardActions.hpp` (`fwCardPlan()`, from crosspad-core);
  `platform/psram_placement.h` (`CROSSPAD_GUI_PSRAM_BSS`).
- `AppOrchestrator::setBeforeLaunchHandler()`.
- `IGuiPlatform::storagePath()`: where a card path is on the platform
  (identity by default).
- `ModeKind::More` and `mode_more`: pad 8 of the MODE map lists every app no
  pad opens.
- `settings_ui` split into one source per category; the public header is
  unchanged.
- `main_screen_has_launcher()` (`main_screen.h`): whether the last
  `build_main_screen()` built a launcher.

### Fixed

- `ui_modal_selection()` with a title answered the option's index plus one.
- `scope_view` allocates its canvas and its SPECTRUM buffers with `lv_malloc`
  instead of an ESP heap call (and builds with MSVC, which has no
  `aligned_alloc`).
