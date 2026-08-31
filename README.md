[![API Docs](https://img.shields.io/badge/API_Docs-GitHub_Pages-blue?logo=github)](https://crosspad.github.io/crosspad-gui/)

# crosspad-gui

Shared LVGL GUI component library for CrossPad. Provides reusable UI widgets, styles, theme system, and application lifecycle management across all CrossPad platforms:

- [ESP32-S3 (Arduino)](https://github.com/CrossPad/ESP32-S3)
- [2playerCrosspad (ESP-IDF)](https://github.com/CrossPad/2playerCrosspad)
- [crosspad-pc (desktop simulator)](https://github.com/CrossPad/crosspad-pc)

Built on LVGL v9.x. Depends on [crosspad-core](https://github.com/CrossPad/crosspad-core) for app interfaces and settings.

## Architecture

```
include/crosspad-gui/
  app/           AppBase CRTP template (app lifecycle)
  components/    High-level UI: launcher, status bar, settings, VU meter, etc.
  platform/      IGuiPlatform, IFileSystem interfaces
  styles/        Global LVGL style objects
  theme/         Color palette, LCD resolution, per-project overrides
  widgets/       Low-level reusable controls
  bootstrap.h    Framework initialization
  crosspad_gui.h Umbrella header
```

## Components

### Main Screen & Launcher

- **main_screen** — standard layout: status bar + app container + launcher grid
- **app_launcher** — flex-wrap button grid of app icons, Power OFF dialog

```cpp
auto result = crosspad_gui::build_main_screen(parent, apps, count, config);
// result.status_bar, result.app_container, result.launcher_grid
```

### Status Bar

Dynamic top bar with app icons (left/right zones), loading progress indicator, and performance stats display. Icons are managed by string key:

```cpp
crosspad_gui::statusbar_add_icon("midi", LV_SYMBOL_SHUFFLE, lv_color_hex(0x00FF00), StatusIconSide::Right);
crosspad_gui::statusbar_set_loading(true, "Updating...");
```

### Settings UI

Unified settings panel shared across all platforms — 7 built-in categories (Display, Audio, Pads & LEDs, Vibration, Connectivity, MIDI, System) plus up to 8 platform-extensible categories.

```cpp
crosspad_gui::settings_ui_create(parent, nullptr);
crosspad_gui::settings_ui_add_category("My Category", my_build_fn);
```

Widget helpers for consistent rows: `settings_row_switch`, `settings_row_slider`, `settings_row_info`, `settings_row_action`, `settings_section_header`.

### VU Meter

Stereo audio level meters with logarithmic scaling and peak hold:

```cpp
crosspad_gui::vu_set_levels(left_db, right_db);
```

### Other Components

| Component | Purpose |
|-----------|---------|
| **radial_menu** | Circular menu with encoder navigation |
| **file_explorer** | Filesystem browser with file type filters and preview |
| **dfu_panel** | Firmware update info screen |
| **ui_modal** | Confirmation and selection dialogs |
| **ui_toast** | Temporary info/success/warning/error messages |
| **volume_overlay** | Volume control with speaker/headphone mode cycling |
| **power_gesture** | Portable power-button resolution: `powerBack()` walks dropdown → modal/overlay → encoder edit → drawer → app `handleBack()`; `powerToggleQuickSettings()` for the hold gesture |

## Widgets

Low-level reusable controls:

| Widget | Purpose |
|--------|---------|
| **keypad_buttons** | 4x4 pad grid with LED color readback |
| **spinbox_helpers** | Styled spinbox builders |
| **settings_widgets** | Settings row builders (switch, slider, text) |
| **footer_button** | Styled footer action button |
| **gui_helpers** | Color utilities, visibility helpers |

## App Framework

`AppBase<Derived>` is a CRTP template providing full LVGL app lifecycle. Each platform defines its own `App` class inheriting from it:

```cpp
class App : public crosspad_gui::AppBase<App> {
    // Platform-specific accessors
};
```

Lifecycle: `start()` -> `pause()` / `resume()` -> `destroyApp()`. Supports flags (HIDDEN, MSGBOX), show/hide callbacks, and auto-registered init/deinit.

## Platform Interfaces

| Interface | Purpose |
|-----------|---------|
| `IGuiPlatform` | Heap stats, millis, asset path prefix, encoder group, perf stats, power off |
| `IFileSystem` | Directory listing abstraction |

Registered during bootstrap:

```cpp
crosspad_gui::bootstrap(&guiPlatform, &fileSystem);
initStyles();  // separate call (may need LVGL mutex on ESP)
```

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

Dark theme with sidebar navigation and hyperlinked source.

## Usage

Include as a git submodule in your platform repo:

```bash
# ESP-IDF component
git submodule add <url> components/crosspad-gui

# Arduino (PlatformIO)
git submodule add <url> lib/crosspad-gui

# Desktop simulator (CMake)
git submodule add <url> crosspad-gui
# Then GLOB_RECURSE crosspad-gui/src/*.cpp in CMakeLists.txt
```

Requires [crosspad-core](https://github.com/CrossPad/crosspad-core) and LVGL v9.x.
