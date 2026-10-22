# Changelog

Notable changes to crosspad-gui. The version is the triple in
`include/crosspad-gui/CrosspadGuiVersion.hpp` (bump rules there;
`library.json` carries the same number and `^` the core it requires, checked
by `scripts/check_manifest.py`). This file starts at 2.0.0; earlier releases
are described in their commits.

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
