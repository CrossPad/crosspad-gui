#ifdef USE_LVGL

#include "crosspad-gui/components/mode_more.h"
#include "crosspad-gui/components/app_launcher.h"

namespace crosspad_gui {

namespace {

const lv_color_t kRow  = lv_color_hex(0x20242A);
const lv_color_t kText = lv_color_hex(0xE8EAED);
const lv_color_t kDim  = lv_color_hex(0x7B828B);

/// Same rule as mode_overlay.cpp's same_name(): ASCII case-insensitive, no
/// <strings.h> (MSVC builds crosspad-pc).
bool same_name(const char* a, const char* b)
{
    if (!a || !b) return false;
    for (; *a && *b; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return *a == *b;
}

bool on_map(const ModeEntry* map, size_t count, const char* name)
{
    for (size_t i = 0; i < count; ++i) {
        if (map[i].app && same_name(map[i].app, name)) return true;
    }
    return false;
}

/// Launcher-only actions never appear in More: one encoder click on a row
/// would run them (Power OFF powers the board off); the power button offers it.
bool launcher_only(const char* name)
{
    return same_name(name, kPowerOffAppName);
}

void on_row_clicked(lv_event_t* e)
{
    auto pick = reinterpret_cast<ModeMorePickFn>(lv_event_get_user_data(e));
    lv_obj_t* row = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const char* name = static_cast<const char*>(lv_obj_get_user_data(row));
    if (pick && name) pick(name);
}

} // namespace

std::vector<const char*> mode_more_apps(const ModeEntry* map, size_t count,
                                        const std::vector<const char*>& registered)
{
    std::vector<const char*> out;
    for (const char* name : registered) {
        if (name && !launcher_only(name) && !on_map(map, count, name)) out.push_back(name);
    }
    return out;
}

lv_obj_t* mode_more_build(lv_obj_t* parent, const std::vector<const char*>& names,
                          ModeMorePickFn onPick, lv_group_t* group)
{
    lv_obj_t* list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    if (names.empty()) {
        lv_obj_t* l = lv_label_create(list);
        lv_obj_set_style_text_color(l, kDim, 0);
        lv_label_set_text(l, "No other apps");
        return list;
    }

    for (const char* name : names) {
        lv_obj_t* row = lv_button_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 32);
        lv_obj_set_style_radius(row, 5, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, kRow, 0);
        lv_obj_set_style_bg_color(row, lv_color_lighten(kRow, 40), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(row, kText, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_border_width(row, 2, LV_STATE_FOCUS_KEY);
        lv_obj_set_user_data(row, const_cast<char*>(name));
        lv_obj_add_event_cb(row, on_row_clicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(onPick));

        lv_obj_t* l = lv_label_create(row);
        lv_obj_set_style_text_color(l, kText, 0);
        lv_label_set_text(l, name);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);

        if (group) lv_group_add_obj(group, row);
    }
    if (group) lv_group_focus_obj(lv_obj_get_child(list, 0));
    return list;
}

} // namespace crosspad_gui

#endif // USE_LVGL
