// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/ui_wifi_bars.h"

#include "crosspad-gui/widgets/gui_helpers.h"

namespace crosspad_gui {

namespace {

constexpr int32_t kBarW = 3;
constexpr int32_t kBarGap = 1;
constexpr int32_t kBarH[crosspad::kWifiBarCount] = {3, 6, 9, 12};
constexpr int32_t kBoxW = crosspad::kWifiBarCount * kBarW + (crosspad::kWifiBarCount - 1) * kBarGap;
constexpr int32_t kBoxH = 12;
constexpr lv_opa_t kDimOpa = LV_OPA_30;
constexpr uintptr_t kNoLook = UINTPTR_MAX;   // user data before the first update
constexpr uintptr_t kVisibleBit = 0x100;
constexpr uintptr_t kRedBit = 0x80;

uintptr_t key_of(const crosspad::WifiBarsLook& look) {
    return (look.visible ? kVisibleBit : 0) | (look.firstRed ? kRedBit : 0) | look.lit;
}

} // namespace

lv_obj_t* ui_wifi_bars_create(lv_obj_t* parent) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, kBoxW, kBoxH);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(box, kBarGap, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    for (int32_t h : kBarH) {
        lv_obj_t* bar = lv_obj_create(box);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, kBarW, h);
        lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(bar, kDimOpa, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_user_data(box, reinterpret_cast<void*>(kNoLook));
    return box;
}

void ui_wifi_bars_update(lv_obj_t* box, bool allowed, crosspad::WifiLinkState state,
                         int8_t rssi, uint32_t tick_ms) {
    const crosspad::WifiBarsLook look = crosspad::wifiBarsLook(allowed, state, rssi, tick_ms);
    const uintptr_t key = key_of(look);
    if (reinterpret_cast<uintptr_t>(lv_obj_get_user_data(box)) == key) return;
    lv_obj_set_user_data(box, reinterpret_cast<void*>(key));
    lv_obj_set_hidden(box, !look.visible);
    for (uint32_t i = 0; i < crosspad::kWifiBarCount; ++i) {
        lv_obj_t* bar = lv_obj_get_child(box, (int32_t)i);
        const bool red = look.firstRed && i == 0;
        lv_obj_set_style_bg_color(bar, red ? lv_palette_main(LV_PALETTE_RED) : lv_color_white(), 0);
        lv_obj_set_style_bg_opa(bar, (red || i < look.lit) ? LV_OPA_COVER : kDimOpa, 0);
    }
}

} // namespace crosspad_gui

#endif // USE_LVGL
