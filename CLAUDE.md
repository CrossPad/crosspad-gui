# CLAUDE.md — CrossPad GUI

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

**crosspad-gui** — shared LVGL UI component library for CrossPad. Provides theme, styles, launcher, status bar, AppBase CRTP template, and reusable widgets (keypad buttons, spinbox, VU meter, file explorer, DFU panel, modals/toasts). Used as a submodule by platform-idf (the ESP-IDF firmware) and crosspad-pc; the Arduino port (ESP32-S3) is deferred. Host tests: `tests/` (see README).

## CrossPad Manifesto

### Write once, run everywhere
CrossPad runs the same core logic on ESP32-S3 hardware and desktop simulators. Platform differences are hidden behind thin abstraction layers — not thick frameworks. Business logic should never know what chip it's running on.

### Platform repos are thin, shared repos are thick
Cross-platform is a first-class goal. Platform-specific repositories (ESP32-S3, 2playerCrosspad, crosspad-pc) should contain only what cannot be shared — hardware drivers, HAL bindings, build system glue. All business logic belongs in crosspad-core; all UI components belong in crosspad-gui. If you're writing code that could work on another platform, it doesn't belong in a platform repo.

### Hardware is software you can touch
We design hardware and software as one system. The pad grid, the encoder, the LED strip — they are first-class citizens with well-defined interfaces. If you can simulate it on a PC, you can ship it on a board.

### Small team, big surface area
We are a small team. This is a feature, not a limitation. Every architectural decision is made to reduce maintenance burden: shared init sequences, unified event buses, portable abstractions. Code that exists in two places will eventually exist in one.

### Open by default
Schematics, firmware, PC tools, documentation — all open source. Not because it's trendy, but because a music controller you can't modify isn't yours. We want people to fork, adapt, and build things we never imagined.

### Community-driven, not committee-driven
We welcome contributions, but we ship fast. A clear architecture and good documentation lower the barrier to entry. You shouldn't need to read the entire codebase to add an app or write a new platform driver.

### Documentation is not an afterthought
If it's not documented, it doesn't exist. API references, architecture guides, build instructions — they ship with the code, not after it.
