# crosspad-gui

Shared LVGL 9 UI library for CrossPad. It holds the launcher, the status bar, the settings screens, `AppBase`, the theme and the reusable widgets, and it builds on [crosspad-core](https://github.com/CrossPad/crosspad-core). The ESP-IDF firmware ([platform-idf](https://github.com/CrossPad/platform-idf), as `components/crosspad-gui`) and the PC simulator ([crosspad-pc](https://github.com/CrossPad/crosspad-pc), as `lib/crosspad-gui`) use the same copy. `library.json` keeps it buildable as a PlatformIO library too, for the [Arduino firmware](https://crosspad.app/docs/legacy/esp32-s3-arduino/).

## Quickstart

A platform builds this library; there is nothing to run on its own. To run the host tests (headless LVGL 9.3 + Catch2; Linux, CMake 3.20+, a C/C++17 compiler with GNU ld):

```bash
cmake -S tests -B tests/build
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

## Documentation

- [Architecture](https://crosspad.app/docs/concepts/architecture/#crosspad-gui-shared-ui), [Where code belongs](https://crosspad.app/docs/concepts/where-code-belongs/) and [Platform parity](https://crosspad.app/docs/concepts/platform-parity/) on https://crosspad.app/docs/.
- [Library reference](#library-reference) below; full API docs: run `doxygen` here and open `docs/doxygen/html/index.html`.
- Contributor notes (build systems, tests, versioning, releases): [CLAUDE.md](CLAUDE.md).

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).

## Library reference

The docs site explains the layers but has no API reference for the UI library, so this overview stays here. The headers are the source of truth.

### Layout

```
include/crosspad-gui/
  app/            AppBase CRTP template, ILvglApp (app lifecycle)
  components/     High-level UI: launcher, status bar, settings, VU meter, scope, transport bar, etc.
  design/         Design language: bitmap fonts and 1-bit icons
  platform/       IGuiPlatform, IFileSystem interfaces, POSIX file system
  styles/         Global LVGL style objects
  theme/          Color palette, LCD resolution, per-project overrides
  virtual_device/ On-screen encoder, pad grid and power button
  widgets/        Low-level reusable controls
  bootstrap.h     Framework initialization
  crosspad_gui.h  Umbrella header
```

### Main screen and launcher

- **main_screen** — standard layout: status bar + app container + launcher (icon grid or list)
- **app_launcher** — flex-wrap button grid of app icons, Power OFF dialog

```cpp
auto result = crosspad_gui::build_main_screen(parent, apps, count, config);
// result.status_bar, result.app_container, result.encoder_group,
// result.launcher_grid (grid style) or result.launcher_menu (list style)
```

### Status bar

Dynamic top bar with app icons (left/right zones), a loading progress indicator and performance stats. Icons are managed by string key:

```cpp
crosspad_gui::statusbar_add_icon("midi", LV_SYMBOL_SHUFFLE, lv_color_hex(0x00FF00), StatusIconSide::Right);
crosspad_gui::statusbar_set_loading(50);   // 0-100 shows progress, -1 hides it
```

### Settings UI

One settings panel for every platform. Built-in categories: Display, Audio, USB, Pads & LEDs, Vibration, MIDI, Analog / Velocity, Backup (SD) (only with `FEAT_SETTINGS_BACKUP`), Programming, Info, System. A platform can add up to 8 categories of its own, before `settings_ui_create()`:

```cpp
crosspad_gui::settings_ui_add_category({"My Category", LV_SYMBOL_SETTINGS, my_build_fn});
crosspad_gui::settings_ui_create(content_area, group);   // group may be nullptr (touch only)
```

Row helpers for consistent rows: `settings_row_switch`, `settings_row_slider`, `settings_row_dropdown`, `settings_row_info`, `settings_row_action`, `settings_section_header`.

### VU meter

Stereo level meters with logarithmic scaling and peak hold. Levels are int16 amplitudes (0–32767):

```cpp
crosspad_gui::vu_set_levels(left, right);
```

### Other components

| Component | Purpose |
|-----------|---------|
| **radial_menu** | Circular menu with encoder navigation |
| **file_explorer** | Filesystem browser with file type filters and preview |
| **dfu_panel** | Firmware update info screen |
| **ui_modal** | Confirmation and selection dialogs |
| **ui_toast** | Temporary info/success/warning/error messages |
| **volume_overlay** | Volume control with speaker/headphone mode cycling |
| **power_gesture** | Portable power-button resolution: `powerBack()` walks dropdown → modal/overlay → encoder edit → drawer → app `handleBack()`; `powerToggleQuickSettings()` for the hold gesture |

More live in `include/crosspad-gui/components/` (scope view, transport bar, tile grid, text prompt, MIDI/BLE panel, STM pad editor, …).

### Widgets

| Widget | Purpose |
|--------|---------|
| **keypad_buttons** | 4x4 pad button grid with pad colors from PadManager |
| **spinbox_helpers** | Styled spinbox builders |
| **settings_widgets** | Settings row builders (switch, slider, text) |
| **footer_button** | Styled footer action button |
| **gui_helpers** | Color utilities, visibility helpers |

### App framework

`AppBase<Derived>` is a CRTP template providing the full LVGL app lifecycle. Each platform defines its own `App` class inheriting from it:

```cpp
class App : public crosspad_gui::AppBase<App> {
    // Platform-specific accessors
};
```

Lifecycle: `start()` -> `pause()` / `resume()` -> `destroyApp()` (or `destroyAppAsync()` from an event callback). Supports app flags (`APP_FLAG_MSGBOX` from crosspad-core) and show/hide callbacks (`setOnShow`, `setOnHide`).

### Platform interfaces

| Interface | Purpose |
|-----------|---------|
| `IGuiPlatform` | Heap stats, millis, delay, asset path prefix, power off, navigation encoder, perf stats; optional LCD brightness, volume, USB audio, STM config and audio-route hooks |
| `IFileSystem` | Directory listing abstraction (`PosixFileSystem` provided) |

Registered during bootstrap:

```cpp
crosspad_gui::bootstrap(&guiPlatform, &fileSystem);
initStyles();  // separate call (may need LVGL mutex on ESP)
```

### Theme

`theme/crosspad_theme.h` defines the colors:

| Macro | Default | Hex |
|-------|---------|-----|
| `CROSSPAD_THEME_PRIMARY` | The palette entry chosen in Settings → Display → Theme Color (default Neon Green) | `#00FF88` |
| `CROSSPAD_THEME_SECONDARY` | White | `#FFFFFF` |
| `CROSSPAD_THEME_ACCENT` | Pink | `#FF0088` |
| `CROSSPAD_THEME_BG_DARK` | Dark background | `#1A1A2E` |
| `CROSSPAD_THEME_CARD_BG` | Card background | `#2A2A2A` |
| `CROSSPAD_THEME_BG_DEEP` / `CARD_DEEP` | Deeper pair for artwork screens (kit browser) | `#0B0B18` / `#16162E` |
| `CROSSPAD_THEME_TEXT` | Text | `#E6E4F0` |

Primary palette: Neon Green `#00FF88`, Yellow `#D0F009`, Cyan `#00FFFF`, Purple `#EA00FF`, Orange `#FF8C00`, Pink `#FF0088`, Blue `#0088FF`, Red `#FF3232`. The index is stored in `CrosspadSettings::themeColorIndex`.

Override per project with a `crosspad_theme_user.h` on the include path: it can `#define` any of the macros above, or `CROSSPAD_THEME_DEFAULT_INDEX` to pick another default palette entry. A project that defines `CROSSPAD_THEME_PRIMARY` gets a fixed primary color instead of the palette.
