# CLAUDE.md — CrossPad GUI

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

**crosspad-gui** — shared LVGL 9 UI component library for CrossPad. Provides theme, styles, launcher, status bar, settings screens, AppBase CRTP template, and reusable widgets (keypad buttons, spinbox, VU meter, file explorer, DFU panel, modals/toasts). Depends on crosspad-core.

User-facing docs live on https://crosspad.app/docs/: [Architecture](https://crosspad.app/docs/concepts/architecture/#crosspad-gui-shared-ui), [Where code belongs](https://crosspad.app/docs/concepts/where-code-belongs/), [Platform parity](https://crosspad.app/docs/concepts/platform-parity/). The README keeps the library reference (components, widgets, platform interfaces, theme colours). A change that breaks a docs page (command, output, path) opens an issue in CrossPad/crosspad-docs.

## Consumers and Build Systems

- **platform-idf** (ESP-IDF firmware): submodule at `components/crosspad-gui/`. `CMakeLists.txt` globs `src/*.cpp` and `src/*.c` (the `.c` files are the generated fonts and icons in `src/design/` and `src/fonts/`) and `REQUIRES lvgl crosspad-core`.
- **crosspad-pc** (simulator): submodule at `lib/crosspad-gui/`; its own CMake globs the same `.cpp`/`.c` files.
- **PlatformIO** (`library.json`, `srcFilter +<*>`): the Arduino firmware (platform-arduino, the legacy ESP32-S3 project), a deferred port that does not track the current API.
- Default branch is `master` (crosspad-core's is `main`).

## Portability

- Most widget and component headers and sources are wrapped in `#ifdef USE_LVGL`; consumers define it (not all files: the platform interfaces, the design fonts/icons and a few components are unguarded).
- No SDL2 or ESP-IDF calls; the one `ESP_PLATFORM` branch is `CROSSPAD_GUI_PSRAM_BSS` in `platform/psram_placement.h`. Host details go through `IGuiPlatform` / crosspad-core's `IFileSystem`.
- `IGuiPlatform`: add new virtuals at the **end** of the class — inserting one mid-class renumbers the vtable, and a translation unit built against an older header calls the wrong slot.

## Tests

Host tests in `tests/`: headless LVGL 9.3.0 (fetched from GitHub, configured only by `tests/lv_conf.h`) + Catch2 v3.5.0, with PNG snapshots. `tests/test_*.cpp` are globbed and the binary links every source in `src/` with crosspad-core's, so a symbol crosspad-gui expects from a platform fails the link. The link uses `-Wl,--wrap=lv_malloc_core`, so it needs GNU ld (Linux). Not run in CI.

```bash
cmake -S tests -B tests/build
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
python3 -m pytest scripts -q          # check_manifest.py's own tests
```

## Versioning and Releases

- The version is the triple in `include/crosspad-gui/CrosspadGuiVersion.hpp` (bump rules in its header comment). The same header names the core it was written for, `CROSSPAD_GUI_REQUIRES_CORE_*`, checked at compile time by `crosspad/RequireVersion.hpp` from `src/CrosspadGuiVersion.cpp`.
- `library.json` must follow the header: `version` equals the triple and `dependencies.crosspad-core` is `^<REQUIRES_CORE>` (platform-idf's dependency resolver reads that range). `python3 scripts/check_manifest.py` checks both; run it before pushing a bump.
- `.github/workflows/lib-release.yml`: a push to `master` that touches `library.json` or `CrosspadGuiVersion.hpp` runs `check_manifest.py`, then tags `vX.Y.Z` through CrossPad/.github's reusable `app-release.yml` in library mode (a bare tag, no GitHub Release). platform-idf's hourly bump reads the tags.
- `.github/workflows/notify-downstream.yml`: a `v*` tag or a merged PR sends a `submodule-update` dispatch to crosspad-pc, ESP32-S3 and matixan/2playerCrosspad.
- `.github/workflows/docs.yml` builds the Doxygen HTML as a workflow artifact; nothing publishes it to GitHub Pages.

## CrossPad Manifesto

The principles behind the layering (write once, run everywhere; thin platform repos; all UI components belong in crosspad-gui; open by default) are on https://crosspad.app/docs/concepts/manifesto/.
