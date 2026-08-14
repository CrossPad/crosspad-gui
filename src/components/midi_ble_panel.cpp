// SPDX-License-Identifier: MIT
//
// MIDI BLE settings panel — shared LVGL UI.
// Platform-specific BLE operations go through MidiBleCallbacks.

#ifdef USE_LVGL

#include <crosspad-gui/components/midi_ble_panel.h>
#include <crosspad-gui/platform/IGuiPlatform.h>
#include <crosspad-gui/app/App.hpp>
#include <crosspad-gui/styles/styles.h>
#include <crosspad-gui/widgets/footer_button.h>
#include <crosspad/event/IEventBus.hpp>
#include <crosspad/pad/PadManager.hpp>
#include <crosspad/settings/CrosspadSettings.hpp>
#include <crosspad/status/CrosspadStatus.hpp>
#include <crosspad/platform/PlatformServices.hpp>

#include <lvgl.h>
#include <cstring>
#include <cstdio>

namespace crosspad_gui {

// ── Platform callbacks ──────────────────────────────────────────────────────

static MidiBleCallbacks s_bleCb;

void midi_ble_set_callbacks(const MidiBleCallbacks& cb) { s_bleCb = cb; }
const MidiBleCallbacks& midi_ble_get_callbacks() { return s_bleCb; }

// ── UI state ────────────────────────────────────────────────────────────────

static lv_obj_t * g_root = nullptr;
static lv_obj_t * g_cont = nullptr;
static lv_obj_t * g_status_lbl = nullptr;
static lv_obj_t * g_ble_switch = nullptr;
static lv_obj_t * g_filter_panel = nullptr;
static lv_obj_t * g_cb_notes = nullptr;
static lv_obj_t * g_cb_sysex = nullptr;
static lv_obj_t * g_cb_other = nullptr;
static lv_obj_t * g_log_box = nullptr;
static lv_obj_t * g_offset_row = nullptr;
static lv_obj_t * g_offset_buttonmatrix = nullptr;
static lv_timer_t * g_timer = nullptr;
static lv_group_t * g_midi_ble_group = nullptr;
static lv_group_t * g_saved_group = nullptr;
static App * g_thisApp = nullptr;

// Subscription handles
static crosspad::SubscriptionHandle g_noteOnHandle = nullptr;
static crosspad::SubscriptionHandle g_noteOffHandle = nullptr;
static crosspad::SubscriptionHandle g_padPressedHandle = nullptr;
static crosspad::SubscriptionHandle g_padReleasedHandle = nullptr;

// Log buffers & filters
#ifndef MIDI_BLE_LOG_SIZE
#define MIDI_BLE_LOG_SIZE 5
#endif
static bool filter_notes = true;
static bool filter_sysex = false;
static bool filter_other = false;

struct Entry { bool tx; const char * type; uint8_t ch; uint8_t d1; uint8_t d2; };
static Entry g_log[MIDI_BLE_LOG_SIZE] = {};
static int g_log_count = 0;

static void push_entry(bool tx, const char * type, uint8_t ch, uint8_t d1, uint8_t d2) {
    Entry e{tx, type, ch, d1, d2};
    for (int i = MIDI_BLE_LOG_SIZE - 1; i > 0; --i) g_log[i] = g_log[i-1];
    g_log[0] = e;
    if (g_log_count < MIDI_BLE_LOG_SIZE) g_log_count++;
}

static bool entry_matches_filters(const Entry &e) {
    bool isNote = (strstr(e.type, "On") || strstr(e.type, "Off"));
    bool isSysEx = (strcmp(e.type, "SysEx") == 0);
    bool isOther = !isNote && !isSysEx;
    if (isNote && filter_notes) return true;
    if (isSysEx && filter_sysex) return true;
    if (isOther && filter_other) return true;
    return false;
}

static void refresh_lists() {
    if (!g_log_box) return;
    lv_obj_clean(g_log_box);
    int shown = 0;
    for (int i = 0; i < g_log_count && shown < MIDI_BLE_LOG_SIZE; ++i) {
        if (!entry_matches_filters(g_log[i])) continue;
        char buf[64];
        const char * dir = g_log[i].tx ? "TX" : "RX";
        if (strstr(g_log[i].type, "On") || strstr(g_log[i].type, "Off")) {
            snprintf(buf, sizeof(buf), "%s %s ch%u n%u v%u", dir, g_log[i].type, g_log[i].ch, g_log[i].d1, g_log[i].d2);
        } else if (strcmp(g_log[i].type, "SysEx") == 0) {
            snprintf(buf, sizeof(buf), "%s SysEx (%uB)", dir, g_log[i].d1);
        } else {
            snprintf(buf, sizeof(buf), "%s %s ch%u %u %u", dir, g_log[i].type, g_log[i].ch, g_log[i].d1, g_log[i].d2);
        }
        lv_obj_t * row = lv_label_create(g_log_box);
        lv_label_set_text(row, buf);
        shown++;
    }
    if (shown == 0) {
        lv_obj_t * row = lv_label_create(g_log_box);
        lv_label_set_text(row, "(empty)");
    }
}

// ── EventBus handlers ───────────────────────────────────────────────────────

static void midi_note_on_rx(const void* event_data, crosspad::EventType, void*) {
    auto *d = static_cast<const crosspad::NoteOnData*>(event_data);
    if (d->source == crosspad::EventSource::MidiInput) {
        push_entry(false, LV_SYMBOL_AUDIO"On", d->channel, d->note, d->velocity);
    }
}

static void midi_note_off_rx(const void* event_data, crosspad::EventType, void*) {
    auto *d = static_cast<const crosspad::NoteOffData*>(event_data);
    if (d->source == crosspad::EventSource::MidiInput) {
        push_entry(false, LV_SYMBOL_AUDIO"Off", d->channel, d->note, d->velocity);
    }
}

static void pad_pressed_tx(const void* event_data, crosspad::EventType, void*) {
    auto *d = static_cast<const crosspad::PadPressedData*>(event_data);
    uint8_t note = crosspad::getPadManager().getMidiNoteForPad(d->padIdx);
    uint8_t ch = 13;
    auto* settings = CrosspadSettings::getInstance();
    if (settings->keypad.send2BLE) push_entry(true, "Pad On", ch, note, d->velocity);
}

static void pad_released_tx(const void* event_data, crosspad::EventType, void*) {
    auto *d = static_cast<const crosspad::PadReleasedData*>(event_data);
    uint8_t note = crosspad::getPadManager().getMidiNoteForPad(d->padIdx);
    uint8_t ch = 13;
    auto* settings = CrosspadSettings::getInstance();
    if (settings->keypad.send2BLE) push_entry(true, "Pad Off", ch, note, 0);
}

// ── UI callbacks ────────────────────────────────────────────────────────────

static void filter_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    filter_notes = lv_obj_has_state(g_cb_notes, LV_STATE_CHECKED);
    filter_sysex = lv_obj_has_state(g_cb_sysex, LV_STATE_CHECKED);
    filter_other = lv_obj_has_state(g_cb_other, LV_STATE_CHECKED);
    refresh_lists();
}

static const uint8_t kOffsetOptions[] = { 36, 52, 68, 84, 100 };
static const char * offsetMap[] = {"36", "52", "68", "84", "100", ""};
static constexpr int kOffsetCount = sizeof(kOffsetOptions) / sizeof(kOffsetOptions[0]);

static int offset_index_for_value(uint8_t val) {
    int idx = 0;
    for (int i = 0; i < kOffsetCount; ++i) {
        if (kOffsetOptions[i] == val) return i;
        if (kOffsetOptions[i] < val) idx = i;
    }
    return idx;
}

static void offset_buttonmatrix_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t * obj = lv_event_get_target_obj(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(obj);
    if (id < (uint32_t)kOffsetCount) {
        auto* statusPtr = crosspad::getPlatformServices().status;
        if (statusPtr) statusPtr->keyOffset = kOffsetOptions[id];
        crosspad::getPadManager().applyDefaultOffsetToPadNotes();
    }
}

static void timer_cb(lv_timer_t *) {
    if (!g_status_lbl) return;
    auto* settings = CrosspadSettings::getInstance();
    bool enabled = settings->keypad.send2BLE;
    if (!enabled) {
        lv_label_set_text(g_status_lbl, "Status: Disabled");
    } else {
        bool connected = s_bleCb.isConnected ? s_bleCb.isConnected() : false;
        lv_label_set_text_fmt(g_status_lbl, "Status: %s", connected ? "Connected" : "Disconnected");
    }
    refresh_lists();
}

static void ble_switch_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t * sw = (lv_obj_t *)lv_event_get_target(e);
    bool v = lv_obj_has_state(sw, LV_STATE_CHECKED);
    auto* settings = CrosspadSettings::getInstance();
    settings->keypad.send2BLE = v;
    if (s_bleCb.setEnabled) s_bleCb.setEnabled(v);
    timer_cb(nullptr);
}

// ── Styles (local) ──────────────────────────────────────────────────────────

static lv_style_t style_bg;
static lv_style_t style_btn;
static lv_style_t style_switch;

// ── Create / Destroy ────────────────────────────────────────────────────────

lv_obj_t* lv_CreateMidiBLE(lv_obj_t * parent, App * a) {
    g_thisApp = a;
    auto* settings = CrosspadSettings::getInstance();
    auto* statusPtr = crosspad::getPlatformServices().status;

    lv_style_init(&style_bg);
    lv_style_set_pad_all(&style_bg, 0);
    lv_style_set_pad_gap(&style_bg, 0);
    lv_style_set_clip_corner(&style_bg, true);
    lv_style_set_radius(&style_bg, 3);
    lv_style_set_border_width(&style_bg, 0);
    lv_style_set_border_post(&style_bg, true);

    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 0);
    lv_style_set_border_width(&style_btn, 1);
    lv_style_set_border_opa(&style_btn, LV_OPA_50);
    lv_style_set_border_color(&style_btn, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_border_side(&style_btn, LV_BORDER_SIDE_INTERNAL);
    lv_style_set_radius(&style_btn, 0);
    lv_style_set_border_post(&style_btn, true);

    lv_style_init(&style_switch);
    lv_style_set_bg_color(&style_switch, lv_color_hex(0xFFFF00));
    lv_style_set_border_color(&style_switch, lv_color_hex(0xFFFF00));

    g_root = lv_obj_create(parent);
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, LV_PCT(100), LV_PCT(100));
    lv_obj_update_layout(g_root);

    g_cont = lv_obj_create(g_root);
    lv_obj_set_size(g_cont, lv_obj_get_width(parent), lv_obj_get_height(parent));
    lv_obj_center(g_cont);
    lv_obj_add_style(g_cont, &styleAppContainer, 0);
    lv_obj_set_style_pad_all(g_cont, 10, 0);
    lv_obj_update_layout(g_root);

    // Title
    lv_obj_t * title = lv_label_create(g_cont);
    lv_label_set_text(title, "MIDI BLE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_update_layout(g_root);

    // Back button
    lv_obj_t * backBtn = lv_CreateFooterButton(g_cont, "Back", -80, [](lv_event_t *) {
        if (g_thisApp && g_thisApp->isStarted()) {
            g_thisApp->destroyApp();
        } else if (g_root) {
            lv_obj_delete_async(g_root);
        }
    });
    lv_obj_align(backBtn, LV_ALIGN_TOP_RIGHT, -12, 8);

    // BLE enable switch
    lv_obj_t * sw = lv_switch_create(g_cont);
    lv_obj_add_state(sw, settings->keypad.send2BLE ? LV_STATE_CHECKED : LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sw, ble_switch_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_style(sw, &style_switch, LV_PART_INDICATOR);
    lv_obj_align_to(sw, backBtn, LV_ALIGN_OUT_LEFT_MID, -7, 0);
    g_ble_switch = sw;

    // Connection status
    g_status_lbl = lv_label_create(g_cont);
    lv_label_set_text(g_status_lbl, "Status: -");
    lv_obj_align_to(g_status_lbl, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    // Note offset: fixed values via button matrix
    g_offset_row = lv_obj_create(g_cont);
    lv_obj_update_layout(g_root);
    lv_obj_add_style(g_offset_row, &styleAppContainer, 0);
    lv_obj_set_height(g_offset_row, 40);
    lv_obj_set_width(g_offset_row, 290);
    {
        lv_obj_update_layout(g_cont);
        lv_obj_t * lbl = lv_label_create(g_offset_row);
        lv_label_set_text(lbl, "Offset:");
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        g_offset_buttonmatrix = lv_buttonmatrix_create(g_offset_row);
        lv_buttonmatrix_set_map(g_offset_buttonmatrix, offsetMap);
        lv_obj_add_style(g_offset_buttonmatrix, &style_bg, 0);
        lv_obj_add_style(g_offset_buttonmatrix, &style_btn, LV_PART_ITEMS);
        lv_obj_add_event_cb(g_offset_buttonmatrix, offset_buttonmatrix_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_set_size(g_offset_buttonmatrix, 170, 26);
        lv_obj_align(g_offset_buttonmatrix, LV_ALIGN_RIGHT_MID, -20, 0);

        lv_buttonmatrix_set_button_ctrl_all(g_offset_buttonmatrix, LV_BUTTONMATRIX_CTRL_CHECKABLE);
        lv_buttonmatrix_set_one_checked(g_offset_buttonmatrix, true);
        uint8_t keyOffset = statusPtr ? statusPtr->keyOffset : 36;
        int initial_idx = offset_index_for_value(keyOffset);
        lv_buttonmatrix_set_button_ctrl(g_offset_buttonmatrix, initial_idx, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    lv_obj_align_to(g_offset_row, g_status_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_update_layout(g_root);

    // Filters + unified log row
    lv_obj_t * filter_log_row = lv_obj_create(g_cont);
    lv_obj_set_size(filter_log_row, LV_PCT(95), 94);
    lv_obj_set_style_pad_all(filter_log_row, 0, 0);
    lv_obj_set_flex_flow(filter_log_row, LV_FLEX_FLOW_ROW);
    lv_obj_align(filter_log_row, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Filter panel (left)
    g_filter_panel = lv_obj_create(filter_log_row);
    lv_obj_set_style_pad_all(g_filter_panel, 4, 0);
    lv_obj_set_flex_flow(g_filter_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(g_filter_panel, 85);
    g_cb_notes = lv_checkbox_create(g_filter_panel); lv_checkbox_set_text(g_cb_notes, "Notes"); lv_obj_add_state(g_cb_notes, LV_STATE_CHECKED);
    g_cb_sysex = lv_checkbox_create(g_filter_panel); lv_checkbox_set_text(g_cb_sysex, "SysEx");
    g_cb_other = lv_checkbox_create(g_filter_panel); lv_checkbox_set_text(g_cb_other, "Other");
    lv_obj_add_event_cb(g_cb_notes, filter_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_cb_sysex, filter_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_cb_other, filter_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Log box (right)
    g_log_box = lv_obj_create(filter_log_row);
    lv_obj_set_style_pad_all(g_log_box, 4, 0);
    lv_obj_set_flex_flow(g_log_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(g_log_box, 1);

    // Subscribe to events
    auto &bus = crosspad::getEventBus();
    g_noteOnHandle = bus.subscribe(crosspad::EventType::NoteOn, midi_note_on_rx, nullptr);
    g_noteOffHandle = bus.subscribe(crosspad::EventType::NoteOff, midi_note_off_rx, nullptr);
    g_padPressedHandle = bus.subscribe(crosspad::EventType::PadPressed, pad_pressed_tx, nullptr);
    g_padReleasedHandle = bus.subscribe(crosspad::EventType::PadReleased, pad_released_tx, nullptr);

    // Timer
    g_timer = lv_timer_create(timer_cb, 200, NULL);

    // Encoder focus group
    g_midi_ble_group = lv_group_create();
    if (g_offset_buttonmatrix) lv_group_add_obj(g_midi_ble_group, g_offset_buttonmatrix);
    if (g_cb_notes) lv_group_add_obj(g_midi_ble_group, g_cb_notes);
    if (g_cb_sysex) lv_group_add_obj(g_midi_ble_group, g_cb_sysex);
    if (g_cb_other) lv_group_add_obj(g_midi_ble_group, g_cb_other);
    if (g_ble_switch) lv_group_add_obj(g_midi_ble_group, g_ble_switch);
    lv_group_add_obj(g_midi_ble_group, backBtn);
    lv_indev_t * nav = getGuiPlatform().getNavigationEncoder();
    if (nav) {
        g_saved_group = lv_indev_get_group(nav);
        lv_indev_set_group(nav, g_midi_ble_group);
    }

    timer_cb(nullptr);
    refresh_lists();
    return g_root;
}

void lv_MidiBLE_destroy(lv_obj_t * obj) {
    // Unsubscribe
    auto &bus = crosspad::getEventBus();
    bus.unsubscribe(g_noteOnHandle);
    bus.unsubscribe(g_noteOffHandle);
    bus.unsubscribe(g_padPressedHandle);
    bus.unsubscribe(g_padReleasedHandle);
    g_noteOnHandle = g_noteOffHandle = g_padPressedHandle = g_padReleasedHandle = nullptr;

    if (g_timer) { lv_timer_del(g_timer); g_timer = nullptr; }

    // Restore encoder group
    lv_indev_t * nav = getGuiPlatform().getNavigationEncoder();
    if (nav && g_saved_group) {
        lv_indev_set_group(nav, g_saved_group);
        g_saved_group = nullptr;
    }
    if (g_midi_ble_group) { lv_group_del(g_midi_ble_group); g_midi_ble_group = nullptr; }

    g_root = nullptr;
    g_cont = nullptr;
    g_status_lbl = nullptr;
    g_offset_row = nullptr;
    g_offset_buttonmatrix = nullptr;
    g_filter_panel = nullptr;
    g_cb_notes = g_cb_sysex = g_cb_other = nullptr;
    g_log_box = nullptr;
    g_log_count = 0;
    g_thisApp = nullptr;

    if (obj && lv_obj_is_valid(obj)) lv_obj_delete_async(obj);
}

} // namespace crosspad_gui

#endif // USE_LVGL
