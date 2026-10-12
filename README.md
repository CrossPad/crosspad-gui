[![API Docs](https://img.shields.io/badge/API_Docs-GitHub_Pages-blue?logo=github)](https://crosspad.github.io/crosspad-gui/)

# crosspad-gui

Shared LVGL GUI library for CrossPad: theme, styles, the launcher and main
screen, status and transport bars, the app lifecycle, settings pages and
reusable widgets. It depends on LVGL v9.3 and
[crosspad-core](https://github.com/CrossPad/crosspad-core) and nothing else --
no ESP-IDF, no FreeRTOS, no globals a platform has to define.

Used by:

- [platform-idf](https://github.com/CrossPad/platform-idf) -- the ESP-IDF firmware (ESP32-S3)
- [crosspad-pc](https://github.com/CrossPad/crosspad-pc) -- the desktop simulator

The Arduino firmware ([ESP32-S3](https://github.com/CrossPad/ESP32-S3)) is a
deferred port and does not track the current API; [Porting to a new
platform](#porting-to-a-new-platform) lists what it has to provide.

## Architecture

```
include/crosspad-gui/
  app/             App, AppBase CRTP template, ILvglApp (app lifecycle)
  components/      Screens and building blocks: launcher, bars, settings, dialogs, ...
  design/          Pixel fonts and 1-bit icons of the design language
  fw/              The Firmware app: Library, Store and Updates views, kit card, actions
  platform/        IGuiPlatform, IFileSystem, GuiServices (what the platform provides)
  styles/          Global LVGL style objects
  theme/           Color palette, LCD resolution, per-project overrides
  virtual_device/  Software-rendered encoder, pad grid and power button
  widgets/         Low-level reusable controls
  bootstrap.h      Framework initialization
  gui_start.h      One-call GUI start (bootstrap + styles + orchestrator + main screen)
  crosspad_gui.h   Umbrella header
src/fonts/         Montserrat 10-24 with European diacritics (replaces LVGL's copies)
cmake/             crosspad_gui_fonts.cmake: takes LVGL's Montserrat out of its build
tools/fonts/       The font generator, its TTF/WOFF sources and their licences
tests/             Host tests (Catch2 + headless LVGL)
```

## Fonts

LVGL ships Montserrat as ASCII plus the FontAwesome symbols. `src/fonts/`
carries the same fonts under the same symbol names (`lv_font_montserrat_10`,
`_12`, `_14`, `_16`, `_18`, `_24`: same TTF, same bpp, same `LV_SYMBOL_*`
list) plus Latin-1 Supplement and Latin Extended-A, so names with diacritics
draw on every platform without a call site naming a special font. A size LVGL's
configuration leaves off compiles to nothing. LVGL's own copies of those sizes
have to leave the build:

```cmake
include(<crosspad-gui>/cmake/crosspad_gui_fonts.cmake)
crosspad_gui_replace_lvgl_fonts(<your LVGL target>)
```

The ESP-IDF component does it for `lvgl__lvgl` and forces each enabled size
into the link from this component. Regenerate with
`python3 tools/fonts/generate.py` (needs node; fetches lv_font_conv through
npx). The range and why it has holes are in the generator; the font licences
are in `tools/fonts/font_license/`.

## Components

### Main Screen & Launcher

- **main_screen** -- standard layout: status bar (or transport bar) + app container + launcher
- **app_launcher** -- grid or list of app icons, Power OFF dialog
- **app_orchestrator** -- which app runs, app factory, pre-launch hooks, foreground tracking

```cpp
auto result = crosspad_gui::build_main_screen(parent, apps, count, config);
// result.status_bar, result.app_container, result.launcher_grid
```

### Status Bar

Top bar with app icons (left/right zones), loading progress and performance
stats. It shows the settings and status the platform handed to
`crosspad_platform_init()` (see [GuiServices](#platform-interfaces)). Icons are
managed by string key:

```cpp
crosspad_gui::statusbar_add_icon("midi", LV_SYMBOL_SHUFFLE, lv_color_hex(0x00FF00), StatusIconSide::Right);
crosspad_gui::statusbar_set_loading(50);
```

### Kit Loader

A kit loading, in the context row (x 4, y 34, 312×22) under the transport bar,
which the status bar's arc no longer is: a mint fill with the text flipping to
ink under it, "LOADING <kit>  38%" → "KIT LOADED  16/16" (fades out after
600 ms); an error is the bar in red until tapped or the next load. On the
overlay layer, built on first use. `statusbar_process_loader_status()` sends
its errors here.

```cpp
crosspad_gui::kit_loader_start("FINGERDRUMMING_BOSS");
crosspad_gui::kit_loader_set(38);          // 0..100
crosspad_gui::kit_loader_done(16, 16);
crosspad_gui::kit_loader_error("MISSING open_hat.wav");
```

### Settings UI

Settings pages shared across platforms -- built-in categories (Display, Audio,
USB, Pads & LEDs, Vibration, MIDI, Analog / Velocity, Backup, Programming,
Info, System), one translation unit each (`src/components/settings_ui*.cpp`),
plus up to 8 platform categories.

```cpp
crosspad_gui::settings_ui_create(parent, nullptr);
crosspad_gui::settings_ui_add_category({"My Category", LV_SYMBOL_SETTINGS, my_build_fn});
```

Row builders: `settings_row_switch`, `settings_row_slider`, `settings_row_info`,
`settings_row_action`, `settings_row_dropdown`, `settings_section_header`.

### Power Button

- **power_gesture** -- what the button means for the UI. `powerBack()` (click)
  walks dropdown -> modal/overlay -> encoder edit mode -> drawer -> top bar
  focus -> the running app's own back -> closing the app;
  `powerToggleQuickSettings()` is the hold. Overlays register with
  `ui_back_push()` / `ui_back_remove()`.
- **power_button_classifier** -- `PowerButtonClassifier`, the click/hold timing
  as a pure class (samples with their time in, gestures out): 500 ms hold
  threshold, 700 ms wake guard, the LIGHT-sleep cancel window. The firmware
  feeds it from its STM32 poll, `powerButtonPress()` / `powerButtonRelease()`
  from an emulated button.

### Firmware App

The firmware launcher: **Library** (packages on the card, the two slots),
**Store** (the cached catalog, Official then Community) and **Updates** tabs,
a full-screen **kit card** whose buttons come from `fwCardPlan()`
(`fw/FwCardActions.hpp`), and the questions before an install. Everything the
views need from the platform goes through `IFwViewService`: storage requests
whose answers move in on `dataPoll()`, the slots and their install/switch
flows, the updater's jobs, the WiFi lease, the network-mode offer and the
FW_UI fields a test reads. The platform registers the app (`REGISTER_APP_PL`,
pad logic `"Firmware"`) around `fw_app_create()` / `fw_app_destroy()` and may
end the Updates tab with a block of its own (`FwAppHooks`).

```cpp
crosspad_gui::setFwViewService(&myService);          // once, after bootstrap()
lv_obj_t* create(lv_obj_t* parent, App* a) { return crosspad_gui::fw_app_create(parent, a, kHooks); }
```

### Other Components

| Component | Purpose |
|-----------|---------|
| **transport_bar** | The top transport row (MODE, REC, play/stop, BPM, position, voices, battery) |
| **boot_splash** | The cold-boot animation: pads light up, logo rises in, backlight and pad LEDs fade along; objects and a compiled-in 6 kB alpha logo, no decoded picture |
| **mode_overlay** | MODE + pad: a 4x4 map of apps and switches, also the home screen |
| **ui_modal** | Confirm, choice and selection dialogs, closed by the power button's back |
| **ui_toast** | Temporary info/success/warning/error messages |
| **ui_text_prompt** | Full-screen text entry (touch keyboard, encoder walk, password mode) |
| **ui_tile_grid** | Virtual grid of tiles: LVGL objects only for the rows in view |
| **ui_wifi_bars** | Four-bar WiFi level indicator |
| **file_explorer** | Filesystem browser with type filters and preview |
| **markdown_view** | Renders headings, bullets, tables and text into a flex column |
| **scope_view** | Oscilloscope of a live audio stream (crosspad::ScopeTap), with an RMS history mode |
| **vu_meter** | Stereo levels with logarithmic scaling and peak hold |
| **volume_overlay** | Output volume with Out 1 / Out 2 cycling |
| **dfu_panel** | Firmware update info screen |
| **midi_ble_panel** | The Settings "BLE MIDI" page (role, transposes, state, scan, devices, connect, monitor) over `MidiBleCallbacks`; rows without a callback are not shown |
| **stm_pad_editor** | Per-pad analog tuning (logic + screen) |
| **touch_gesture** | Screen-wide swipes, recognised beneath LVGL's object layer |
| **app_icon** | Icon path resolution against the platform's asset prefix |
| **settings_app** | The Settings app shell around settings_ui |

## Widgets

| Widget | Purpose |
|--------|---------|
| **keypad_buttons** | 4x4 pad button grid in the pads' colors |
| **spinbox_helpers** | Styled spinbox builders |
| **settings_widgets** | Text, switch and slider rows bound to a setting |
| **footer_button** | Styled footer action button |
| **gui_helpers** | Color utilities, visibility helpers, guarded encoder-group restore |

## App Framework

`App` (`crosspad-gui/app/App.hpp`) is the app class every platform uses:
`AppBase<App>` (CRTP) gives it the LVGL lifecycle, and its accessors
(`padManager()`, `ledController()`, `status()`, `settings()`) read
crosspad-core's `PlatformServices`, so a platform implements nothing for it.

Lifecycle: `start()` -> `pause()` / `resume()` -> `destroyApp()`. An app that
declares a pad logic name gets `pushPadLogic()` on start and `popPadLogic()` on
destroy; a root deleted by someone else (a launcher rebuild) rolls the app back
to not-started through its `LV_EVENT_DELETE` hook and calls the app's destroy
function with `nullptr`: release timers, subscriptions and groups, delete
nothing. A destroy function therefore deletes its root only when it gets one
(`if (root) lv_obj_delete_async(root);`).

## Platform Interfaces

| Interface | Purpose |
|-----------|---------|
| `IGuiPlatform` | Heap stats, millis, asset path prefix, navigation encoder, perf stats, power off, volumes, STM config, audio routing, ... |
| `IFileSystem` | Directory listing (`PosixFileSystem` is the POSIX one) |
| `GuiServices` | `platformSettings()` / `platformStatus()`: the settings and status in `PlatformServices` |
| `IFwViewService` | The Firmware app's card, slots, updater, WiFi lease and network mode (`fw/IFwViewService.h`); only for a platform that hosts it |
| `CROSSPAD_GUI_PSRAM_BSS` | Static state placed in external RAM (`platform/psram_placement.h`): `EXT_RAM_BSS_ATTR` under ESP-IDF, empty elsewhere |

## Porting to a New Platform

A platform provides:

1. crosspad-core initialised -- `crosspad_platform_init()` with its event bus,
   clock, LED strip, MIDI output, pad objects, and the `CrosspadSettings` /
   `CrosspadStatus` the GUI is to show -- before the GUI starts;
2. an `IGuiPlatform` and an `IFileSystem` implementation;
3. `crosspad_gui::gui_start()` (or `bootstrap()` + `initStyles()` + the
   orchestrator by hand), then its own LVGL loop;
4. LVGL 9.3 with Montserrat 10, 14, 16, 18 and 24 enabled, and LVGL's own
   copies taken out of its build ([Fonts](#fonts)).

Nothing else: no global `settings`, `status` or `status_c`, no `App`
accessors (crosspad-gui defines them; a second definition is a duplicate
symbol).

## Theme System

Color palette defined in `crosspad_theme.h`:

| Color | Default | Hex |
|-------|---------|-----|
| Primary | Neon green | `#00FF66` |
| Secondary | White | `#FFFFFF` |
| Accent | Pink | `#FF3399` |
| Background | Dark | `#1A1A2E` |
| Card | Darker | `#16213E` |

Override per-project by providing `crosspad_theme_user.h` with custom `#define` values before including the theme header.

## API Documentation

Generate browsable API docs with [Doxygen](https://www.doxygen.nl/):

```bash
doxygen
# open docs/doxygen/html/index.html
```

## Usage

Include as a git submodule in your platform repo:

```bash
# ESP-IDF component
git submodule add <url> components/crosspad-gui

# Desktop (CMake)
git submodule add <url> lib/crosspad-gui
# Then GLOB_RECURSE lib/crosspad-gui/src/*.cpp and *.c in CMakeLists.txt,
# and crosspad_gui_replace_lvgl_fonts() on the LVGL target (see Fonts)
```

## Tests

Host tests: Catch2 and LVGL 9.3 built headless, linked with crosspad-core's
portable sources. The binary links every source in `src/`, so the build also
fails if crosspad-gui expects a symbol from a platform.

```bash
cmake -S tests -B tests/build                 # crosspad-core beside this repo, or
cmake -S tests -B tests/build -DCROSSPAD_CORE_DIR=/path/to/crosspad-core
cmake --build tests/build -j8
ctest --test-dir tests/build --output-on-failure
```

## Contributing / CI

- `.github/workflows/docs.yml` builds the Doxygen pages on pushes to `master`.
- `.github/workflows/notify-downstream.yml` tells the platform repos that
  crosspad-gui moved (on a `v*` tag, a merged PR, or by hand). It needs the
  repository secret `DOWNSTREAM_PAT` (a token that can dispatch to those
  repos); the optional repository variable `CI_SECRETS_RUNNER` picks its runner.
  Without the secret -- in a fork -- the job leaves a notice and does nothing.

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).
