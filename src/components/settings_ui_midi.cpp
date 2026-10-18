// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: the MIDI category.

#ifdef USE_LVGL

#include "settings_ui_internal.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/stm32/reg_map.h"

using namespace crosspad;

namespace crosspad_gui {

using namespace settings_detail;

// ---- MIDI ----

// STM note-offset presets (semitones). int8_t field <- dropdown index.
static const int8_t kNoteOffsetVals[] = { -24, -12, -7, -5, 0, 5, 7, 12, 24 };
static uint8_t s_noteOffsetIdx = 4;  // default index -> 0 semitones

static void stm_midi_chan_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_MIDI_CHAN, s->stm.midiChannel);
}
static void stm_note_offset_cb(lv_event_t*) {
    auto* s = get_settings(); if (!s) return;
    if (s_noteOffsetIdx >= (sizeof(kNoteOffsetVals)/sizeof(kNoteOffsetVals[0]))) s_noteOffsetIdx = 4;
    s->stm.noteOffset = kNoteOffsetVals[s_noteOffsetIdx];
    crosspad_gui::getGuiPlatform().writeStmConfig8(REG_CTL_NOTE_OFFSET, (uint8_t)s->stm.noteOffset);
}

void settings_detail::cat_build_midi(lv_obj_t * parent, lv_group_t * group) {
    auto* s = get_settings();
    if (!s) return;

    settings_section_header(parent, "Keypad Routing");

    settings_row_switch(parent, group, "Send to STM",
                        &s->keypad.send2STM);

    settings_row_switch(parent, group, "Send to BLE",
                        &s->keypad.send2BLE);

    settings_row_switch(parent, group, "Send to USB",
                        &s->keypad.send2USB);

    settings_row_switch(parent, group, "Send CC",
                        &s->keypad.sendCC);

    settings_section_header(parent, "STM32 Routing");

    settings_row_switch(parent, group, "Routing Enabled",
                        &s->stmRouting.RoutingMatrixEnabled);

    settings_row_switch(parent, group, "MIDI -> Thru",
                        &s->stmRouting.Midi2MidiThru);

    settings_row_switch(parent, group, "MIDI -> USB",
                        &s->stmRouting.Midi2USB);

    settings_row_switch(parent, group, "MIDI -> ESP",
                        &s->stmRouting.Midi2ESP);

    settings_row_switch(parent, group, "USB -> MIDI",
                        &s->stmRouting.USB2Midi);

    settings_row_switch(parent, group, "USB -> Thru",
                        &s->stmRouting.USB2USBThru);

    settings_row_switch(parent, group, "USB -> ESP",
                        &s->stmRouting.USB2ESP);

    settings_row_switch(parent, group, "ESP -> MIDI",
                        &s->stmRouting.ESP2Midi);

    settings_row_switch(parent, group, "ESP -> USB",
                        &s->stmRouting.ESP2USB);

    settings_row_switch(parent, group, "ESP -> Thru",
                        &s->stmRouting.ESP2ESPThru);

    settings_row_switch(parent, group, "Buttons -> MIDI",
                        &s->stmRouting.Buttons2Midi);

    settings_row_switch(parent, group, "Buttons -> USB",
                        &s->stmRouting.Buttons2USB);

    settings_row_switch(parent, group, "Buttons -> ESP",
                        &s->stmRouting.Buttons2ESP);

    settings_row_switch(parent, group, "Config Btns as MIDI",
                        &s->stmRouting.ConfigButtonsAsMidi);

    settings_section_header(parent, "ESP32 Routing");

    // BLE2Audio / Midi2Audio have no consumer in the firmware — only the two
    // forwarding paths below are wired (MidiInputHandler).
    settings_row_switch(parent, group, "BLE -> MIDI",
                        &s->esp32Routing.BLE2Midi);

    settings_row_switch(parent, group, "MIDI -> BLE",
                        &s->esp32Routing.Midi2BLE);

    settings_section_header(parent, "STM Pads (MIDI)");
    // MIDI channel 0..15 (display 1..16 is fine to leave as 0..15 raw)
    settings_row_slider(parent, group, "STM MIDI Channel", 0, 15,
                        &s->stm.midiChannel, stm_midi_chan_cb);
    // Note offset preset dropdown -> kNoteOffsetVals
    {
        // sync proxy index from the stored value
        s_noteOffsetIdx = 4;
        for (uint8_t i = 0; i < sizeof(kNoteOffsetVals)/sizeof(kNoteOffsetVals[0]); ++i)
            if (kNoteOffsetVals[i] == s->stm.noteOffset) { s_noteOffsetIdx = i; break; }
        settings_row_dropdown(parent, group, "Note Offset",
                              "-24\n-12\n-7\n-5\n0\n+5\n+7\n+12\n+24",
                              &s_noteOffsetIdx, stm_note_offset_cb);
    }
}

} // namespace crosspad_gui

#endif // USE_LVGL
