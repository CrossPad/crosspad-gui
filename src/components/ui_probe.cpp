#ifdef USE_LVGL

#include "crosspad-gui/components/ui_probe.h"

#include "crosspad-gui/platform/IGuiPlatform.h"

#include "src/core/lv_obj_class_private.h"

#include <cstdio>
#include <cstring>

namespace crosspad_gui {

namespace {

constexpr int32_t kLcdW = 320;
constexpr int32_t kLcdH = 240;
constexpr int kTextDepth = 3;

/// "lv_button" -> "button": the prefix is the library's, not the widget's.
const char* class_name(const lv_obj_class_t* cls)
{
    if (!cls || !cls->name) return "obj";
    return strncmp(cls->name, "lv_", 3) == 0 ? cls->name + 3 : cls->name;
}

bool is_visible(lv_obj_t* o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
    if (lv_obj_get_style_opa_recursive(o, LV_PART_MAIN) == 0) return false;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    return a.x2 >= 0 && a.y2 >= 0 && a.x1 < kLcdW && a.y1 < kLcdH;
}

const char* label_text(lv_obj_t* o, int depth)
{
    if (lv_obj_check_type(o, &lv_label_class)) return lv_label_get_text(o);
    if (depth == 0) return nullptr;
    const uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* c = lv_obj_get_child(o, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        if (const char* t = label_text(c, depth - 1)) return t;
    }
    return nullptr;
}

struct Walk {
    UiNode* out;
    uint16_t max;
    uint16_t n;
    lv_group_t* nav;
};

void add(Walk& w, lv_obj_t* o, int16_t parent)
{
    if (w.n >= w.max || !is_visible(o)) return;

    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const lv_obj_class_t* cls = lv_obj_get_class(o);
    lv_group_t* g = lv_obj_get_group(o);

    UiNode& nd = w.out[w.n];
    nd.cls = class_name(cls);
    nd.x = a.x1;
    nd.y = a.y1;
    nd.w = lv_area_get_width(&a);
    nd.h = lv_area_get_height(&a);
    nd.parent = parent;
    nd.text = label_text(o, kTextDepth);
    nd.interactive = lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE) || (w.nav && g == w.nav);
    nd.checked = lv_obj_has_state(o, LV_STATE_CHECKED);
    nd.disabled = lv_obj_has_state(o, LV_STATE_DISABLED);
    nd.focused = lv_obj_has_state(o, LV_STATE_FOCUSED);

    const int16_t self = static_cast<int16_t>(w.n++);
    const uint32_t kids = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < kids; ++i) add(w, lv_obj_get_child(o, i), self);
}

void put_string(std::string& out, const char* s)
{
    out += '"';
    for (const char* p = s; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20) {
            char u[7];
            snprintf(u, sizeof u, "\\u%04x", c);
            out += u;
        } else {
            out += static_cast<char>(c);
        }
    }
    out += '"';
}

void put_bool(std::string& out, const char* key, bool v)
{
    out += ",\"";
    out += key;
    out += v ? "\":true" : "\":false";
}

}  // namespace

uint16_t ui_tree_probe(UiNode* out, uint16_t max)
{
    if (!out || max == 0) return 0;
    lv_indev_t* enc = getGuiPlatform().getNavigationEncoder();
    Walk w{out, max, 0, enc ? lv_indev_get_group(enc) : nullptr};
    add(w, lv_screen_active(), -1);
    add(w, lv_layer_top(), -1);
    return w.n;
}

void ui_tree_json(const UiNode* n, uint16_t count, const char* running, std::string& out)
{
    out += "{\"running\":";
    put_string(out, running ? running : "");
    out += ",\"lcd\":{\"w\":" + std::to_string(kLcdW) + ",\"h\":" + std::to_string(kLcdH) + "},\"nodes\":[";
    for (uint16_t i = 0; i < count; ++i) {
        const UiNode& nd = n[i];
        char num[96];
        if (i) out += ',';
        out += "{\"cls\":";
        put_string(out, nd.cls ? nd.cls : "obj");
        snprintf(num, sizeof num, ",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"parent\":%d,\"text\":",
                 static_cast<int>(nd.x), static_cast<int>(nd.y), static_cast<int>(nd.w),
                 static_cast<int>(nd.h), static_cast<int>(nd.parent));
        out += num;
        if (nd.text) put_string(out, nd.text);
        else out += "null";
        put_bool(out, "interactive", nd.interactive);
        put_bool(out, "checked", nd.checked);
        put_bool(out, "disabled", nd.disabled);
        put_bool(out, "focused", nd.focused);
        out += '}';
    }
    out += "]}";
}

}  // namespace crosspad_gui

#endif
