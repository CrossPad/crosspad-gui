#pragma once

#ifdef USE_LVGL
#include <cstddef>
#include <cstdint>

namespace crosspad_gui {

enum TextPromptFlags : uint32_t {
    UI_TEXT_PASSWORD = 1u << 0,   // dots, with a show/hide toggle
};

// ok=false (cancel or power-button back) passes "". `text` is valid only
// during the call. Runs on the LVGL thread, exactly once.
using TextPromptDone = void (*)(bool ok, const char* text, void* user);

// Full-screen text entry on the overlay parent: title, one-line field,
// keyboard. Touch types; the encoder walks the keys and a press presses; a
// power click cancels. The encoder group in place before is put back after.
void ui_text_prompt(const char* title, const char* initial, uint32_t flags,
                    size_t max_len, TextPromptDone done, void* user);

} // namespace crosspad_gui

#endif // USE_LVGL
