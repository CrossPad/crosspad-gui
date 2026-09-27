#ifdef USE_LVGL

#include "crosspad-gui/components/app_launcher.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad/kit/IKitManager.hpp"
#include "crosspad/kit/KitInfo.hpp"
#include "crosspad/platform/PlatformServices.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace crosspad_gui {

// ── Menu ────────────────────────────────────────────────────────────────────
//
// The "magnifier" list: every app is a slim row, the one under the encoder
// grows into a lime card with its icon in a chip, its name and a line saying
// what it does. Turning the encoder moves the card; pressing opens it. On the
// touch screen the first tap selects a row and a tap on the selected one opens
// it, so a finger can look before it commits.

namespace {

// Geometry and palette of the design.
constexpr int32_t kRowH          = 18;
constexpr int32_t kRowSelH       = 42;
constexpr int32_t kRowRadius     = 6;
constexpr int32_t kRowSelRadius  = 13;
constexpr int32_t kRowPadH       = 9;
constexpr int32_t kRowSelPadH    = 8;
constexpr int32_t kRowGap        = 3;
constexpr int32_t kRowIconGap    = 8;
constexpr int32_t kListPadH      = 9;
constexpr int32_t kListPadV      = 6;
constexpr int32_t kHeaderH       = 18;
constexpr int32_t kHeaderPadTop  = 4;
constexpr int32_t kHeaderMarkW   = 3;
constexpr int32_t kHeaderMarkH   = 12;
constexpr int32_t kHeaderMarkR   = 2;
constexpr int32_t kHeaderKitX    = 10;
constexpr int32_t kHeaderKitW    = 220;
constexpr int32_t kIcon          = 12;
constexpr int32_t kIconSel       = 16;
constexpr int32_t kChipSel       = 26;
constexpr int32_t kChipSelRadius = 9;
constexpr int32_t kTextGap       = 1;
constexpr lv_opa_t kChipSelOpa   = 36;    // ~14 % ink on the lime card
constexpr lv_opa_t kMetaOpa      = 179;   // ~70 %
/// Rows the menu holds; the registry holds 16 apps, plus the built-in Power.
constexpr size_t  kMaxRows       = 24;
/// An icon path: the platform's asset prefix (a whole absolute directory on
/// the simulator) and the file name.
constexpr size_t  kIconPathLen   = 256;

constexpr uint32_t kBg       = 0x16181B;
constexpr uint32_t kRow      = 0x20242A;
constexpr uint32_t kAccent   = 0xC6FF47;
constexpr uint32_t kInk      = 0x16181B;
constexpr uint32_t kLabelDim = 0x9AA0A8;
constexpr uint32_t kBarText  = 0xE8EAED;
constexpr uint32_t kBarMeta  = 0x7B828B;

/// How an app is shown in the menu, keyed by the name it registers under.
///
/// THE ORDER OF THIS TABLE IS THE ORDER OF THE MENU -- move a line to move the
/// row. An app not listed here goes after all of these, in registry priority
/// order, with its own name and icon and a neutral colour.
///
/// The ic_*.png icons are 16 px on purpose: a 64 px app icon drawn at 12 px
/// still decodes at 64 px into LVGL's image cache, sixteen times the PSRAM.
struct MenuLook {
    const char* app;
    const char* label;
    uint32_t    color;
    const char* meta;
    const char* icon;   // asset file name, or null for the app's own icon
};

const MenuLook kLooks[] = {
    {"KitSelector",  "Kits",         0x5AC8FF, "browse and load a kit",      "ic_kits.png"},
    {"Sampler",      "Sampler",      0xC6FF47, "play the kit on the pads",   "ic_sampler.png"},
    {"Sequencer",    "Sequencer",    0xFF7A9A, "record and loop the pads",   "ic_sequencer.png"},
    {"Song",         "Song",         0xF26DF9, "scenes arranged in a row",   "ic_song.png"},
    {"Arrange",      "Arrange",      0x4FC3F7, "audio tracks on a timeline", "ic_arrange.png"},
    {"Recorder",     "Recorder",     0xFF5C5C, "record a sample, trim a loop", "ic_recorder.png"},
    {"Slicer",       "Slicer",       0xFFB347, "chop a loop into a kit",     "ic_slicer.png"},
    {"PadMixer",     "Pad Mixer",    0x7AA2FF, "a fader for every pad",      nullptr},
    {"Mixer",        "Mixer",        0xFFC857, "channels and outputs",       "ic_mixer.png"},
    {"DawControl",   "DAW Control",  0xA0E7A0, "FL Studio, Ableton",         nullptr},
    {"Firmware",     "Firmware",     0xFF9552, "slots, library, update",     "ic_update.png"},
    {"Instructions", "Instructions", 0x4FE0B0, "shortcuts and diagrams",     "ic_help.png"},
    {"Settings",     "Settings",     0xB48CFF, "audio, MIDI, display",       "ic_settings.png"},
    {"Power OFF",    "Power",        0xE8EAED, "shut down",                  "ic_power.png"},
};

const MenuLook* look_for(const char* name) {
    for (const auto& l : kLooks) {
        if (name && strcmp(l.app, name) == 0) return &l;
    }
    return nullptr;
}

struct Row {
    lv_obj_t* row  = nullptr;
    lv_obj_t* chip = nullptr;
    lv_obj_t* icon = nullptr;
    lv_obj_t* name = nullptr;
    lv_obj_t* meta = nullptr;
    void*     app  = nullptr;
    uint32_t  color = kBarText;
    bool      glyph = false;   // icon is a font symbol, not an image
    bool      tint  = false;   // a flat icon drawn in the row's colours
};

LauncherConfig s_config = {};
Row      s_rows[kMaxRows];
size_t   s_rowCount = 0;
/* Survives a rebuild: coming back from an app lands on the row it was
 * opened from. */
size_t   s_sel = 0;
lv_obj_t* s_list = nullptr;
lv_obj_t* s_appContainer = nullptr;
lv_obj_t* s_barMark = nullptr;
lv_obj_t* s_barPos = nullptr;
/* A tap that only moved the selection must not also open the row. */
bool     s_focusedByPointer = false;

bool s_stylesReady = false;
lv_style_t s_stRow, s_stRowSel, s_stChipSel;

void styles_init() {
    if (s_stylesReady) return;
    s_stylesReady = true;

    lv_style_init(&s_stRow);
    lv_style_set_bg_color(&s_stRow, lv_color_hex(kRow));
    lv_style_set_bg_opa(&s_stRow, LV_OPA_COVER);
    lv_style_set_radius(&s_stRow, kRowRadius);
    lv_style_set_border_width(&s_stRow, 0);
    lv_style_set_pad_hor(&s_stRow, kRowPadH);
    lv_style_set_pad_ver(&s_stRow, 0);
    lv_style_set_pad_column(&s_stRow, kRowIconGap);

    lv_style_init(&s_stRowSel);
    lv_style_set_bg_color(&s_stRowSel, lv_color_hex(kAccent));
    lv_style_set_radius(&s_stRowSel, kRowSelRadius);
    lv_style_set_pad_hor(&s_stRowSel, kRowSelPadH);

    lv_style_init(&s_stChipSel);
    lv_style_set_bg_color(&s_stChipSel, lv_color_hex(kInk));
    lv_style_set_bg_opa(&s_stChipSel, kChipSelOpa);
    lv_style_set_radius(&s_stChipSel, kChipSelRadius);
}

void apply(size_t i, bool on) {
    Row& r = s_rows[i];
    lv_obj_set_height(r.row, on ? kRowSelH : kRowH);
    if (on) lv_obj_add_style(r.row, &s_stRowSel, 0);
    else    lv_obj_remove_style(r.row, &s_stRowSel, 0);

    const int32_t chip = on ? kChipSel : kIcon;
    lv_obj_set_size(r.chip, chip, chip);
    if (on) lv_obj_add_style(r.chip, &s_stChipSel, 0);
    else    lv_obj_remove_style(r.chip, &s_stChipSel, 0);

    const lv_color_t ic = on ? lv_color_hex(kInk) : lv_color_hex(r.color);
    if (r.glyph) {
        lv_obj_set_style_text_font(r.icon, on ? &lv_font_montserrat_14 : &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(r.icon, ic, 0);
    } else {
        const int32_t sz = on ? kIconSel : kIcon;
        lv_obj_set_size(r.icon, sz, sz);
        if (r.tint) lv_obj_set_style_image_recolor(r.icon, ic, 0);
    }
    lv_obj_center(r.icon);

    lv_obj_set_style_text_font(r.name, on ? &lv_font_montserrat_14 : &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(r.name, on ? lv_color_hex(kInk) : lv_color_hex(kLabelDim), 0);

    if (on) lv_obj_remove_flag(r.meta, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(r.meta, LV_OBJ_FLAG_HIDDEN);
}

void refresh_header() {
    if (s_barMark) lv_obj_set_style_bg_color(s_barMark, lv_color_hex(s_rows[s_sel].color), 0);
    if (s_barPos) lv_label_set_text_fmt(s_barPos, "%u / %u",
                                        (unsigned)(s_sel + 1), (unsigned)s_rowCount);
}

void select_row(size_t i) {
    if (i >= s_rowCount) return;
    if (i != s_sel && s_sel < s_rowCount) apply(s_sel, false);
    s_sel = i;
    apply(s_sel, true);
    refresh_header();
    /* After the resize: LVGL's own scroll-on-focus ran before it, against the
     * collapsed height, and would leave the card half off the bottom. */
    lv_obj_update_layout(s_list);
    lv_obj_scroll_to_view(s_rows[i].row, LV_ANIM_OFF);
}

void row_focused_cb(lv_event_t* e) {
    const size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e);
    lv_indev_t* indev = lv_indev_active();
    /* LVGL sends FOCUSED on every press of a group member, also when it is
     * focused already: only a tap that moves the selection is "only moved". */
    s_focusedByPointer = indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER && i != s_sel;
    select_row(i);
}

void row_clicked_cb(lv_event_t* e) {
    const size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e);
    lv_indev_t* indev = lv_indev_active();
    const bool pointer = indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER;
    if (pointer && (s_focusedByPointer || i != s_sel)) {
        s_focusedByPointer = false;
        select_row(i);
        return;
    }
    s_focusedByPointer = false;
    if (s_rows[i].app && s_config.on_select) s_config.on_select(s_rows[i].app, s_appContainer);
}

void menu_deleted_cb(lv_event_t* e) {
    /* The launcher is torn down by lv_obj_clean() of the whole screen; the
     * table must not outlive the rows it points at. */
    (void)e;
    s_rowCount = 0;
    s_list = s_barMark = s_barPos = s_appContainer = nullptr;
}

lv_obj_t* make_box(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

void build_header(lv_obj_t* wrap) {
    lv_obj_t* bar = make_box(wrap);
    lv_obj_set_size(bar, LV_PCT(100), kHeaderH);
    lv_obj_set_style_pad_hor(bar, kListPadH, 0);
    lv_obj_set_style_pad_top(bar, kHeaderPadTop, 0);

    s_barMark = make_box(bar);
    lv_obj_set_size(s_barMark, kHeaderMarkW, kHeaderMarkH);
    lv_obj_set_style_radius(s_barMark, kHeaderMarkR, 0);
    lv_obj_set_style_bg_opa(s_barMark, LV_OPA_COVER, 0);
    lv_obj_align(s_barMark, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* kit = lv_label_create(bar);
    lv_obj_set_style_text_font(kit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(kit, lv_color_hex(kBarText), 0);
    lv_label_set_long_mode(kit, LV_LABEL_LONG_DOT);
    lv_obj_set_width(kit, kHeaderKitW);
    crosspad::IKitManager* km = crosspad::getKitManager();
    crosspad::KitInfo* ki = km ? km->getCurrentKit() : nullptr;
    lv_label_set_text(kit, ki ? ki->name.c_str() : "No kit");
    lv_obj_align(kit, LV_ALIGN_LEFT_MID, kHeaderKitX, 0);

    s_barPos = lv_label_create(bar);
    lv_obj_set_style_text_font(s_barPos, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_barPos, lv_color_hex(kBarMeta), 0);
    lv_obj_align(s_barPos, LV_ALIGN_RIGHT_MID, 0, 0);
}

void add_row(const LauncherAppInfo& info, const char* prefix) {
    const size_t i = s_rowCount++;
    Row& r = s_rows[i];
    r = Row{};
    r.app = info.app_ptr;

    const MenuLook* look = look_for(info.name);
    r.color = look ? look->color : kBarText;

    /* lv_image_set_src() keeps its own copy of a file path, so the buffer
     * only has to outlive this call. */
    char iconPath[kIconPathLen];
    const char* iconSrc = info.icon;
    if (look && look->icon) {
        snprintf(iconPath, sizeof(iconPath), "%s%s", prefix, look->icon);
        iconSrc = iconPath;
    }
    r.glyph = iconSrc && (uint8_t)iconSrc[0] >= 0x80;
    /* The menu's own icons are flat glyphs made to be tinted; an app's own
     * icon only when it asked for it (AppEntry::recolorIcon). */
    r.tint = (look && look->icon) || info.recolor_icon;

    r.row = make_box(s_list);
    lv_obj_add_style(r.row, &s_stRow, 0);
    lv_obj_set_size(r.row, LV_PCT(100), kRowH);
    lv_obj_set_flex_flow(r.row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(r.row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r.row, row_focused_cb, LV_EVENT_FOCUSED, (void*)(uintptr_t)i);
    lv_obj_add_event_cb(r.row, row_clicked_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

    /* The row is what a tap lands on: its boxes pass the click through. */
    r.chip = make_box(r.row);
    lv_obj_remove_flag(r.chip, LV_OBJ_FLAG_CLICKABLE);
    if (r.glyph) {
        r.icon = lv_label_create(r.chip);
        lv_label_set_text(r.icon, iconSrc);
    } else {
        r.icon = lv_image_create(r.chip);
        if (iconSrc) lv_image_set_src(r.icon, iconSrc);
        lv_image_set_inner_align(r.icon, LV_IMAGE_ALIGN_CONTAIN);
        if (r.tint) lv_obj_set_style_image_recolor_opa(r.icon, LV_OPA_COVER, 0);
    }

    lv_obj_t* txt = make_box(r.row);
    lv_obj_remove_flag(txt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_grow(txt, 1);
    lv_obj_set_height(txt, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(txt, kTextGap, 0);

    r.name = lv_label_create(txt);
    lv_label_set_long_mode(r.name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r.name, LV_PCT(100));
    lv_label_set_text(r.name, look ? look->label : info.name);

    r.meta = lv_label_create(txt);
    lv_obj_set_style_text_font(r.meta, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(r.meta, lv_color_hex(kInk), 0);
    lv_obj_set_style_text_opa(r.meta, kMetaOpa, 0);
    lv_label_set_long_mode(r.meta, LV_LABEL_LONG_DOT);
    lv_obj_set_width(r.meta, LV_PCT(100));
    lv_label_set_text(r.meta, look ? look->meta : "");

    apply(i, false);
    /* Last: the group focuses its first member as it is added, and that
     * FOCUSED expands a row -- which needs its children to exist. */
    if (s_config.encoder_group) lv_group_add_obj(s_config.encoder_group, r.row);
}

} // namespace

lv_obj_t* launcher_create_menu(
    lv_obj_t* parent,
    const LauncherAppInfo* apps,
    size_t app_count,
    const LauncherConfig* config)
{
    s_config = *config;  // copy by value — safe even if caller passes a stack-local config
    styles_init();
    s_appContainer = parent;
    s_rowCount = 0;
    /* The row to come back to; nothing is expanded until one is focused. */
    size_t want = s_sel;
    s_sel = kMaxRows;

    lv_obj_t* wrap = make_box(parent);
    lv_obj_set_size(wrap, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wrap, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(wrap, menu_deleted_cb, LV_EVENT_DELETE, nullptr);

    build_header(wrap);

    /* The list scrolls when there are more apps than fit; the encoder keeps
     * the card in view, a scrollbar would only take width. */
    s_list = make_box(wrap);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(s_list, kListPadH, 0);
    lv_obj_set_style_pad_ver(s_list, kListPadV, 0);
    lv_obj_set_style_pad_row(s_list, kRowGap, 0);

    /* The table's order first, then whatever it does not name. */
    size_t order[kMaxRows];
    size_t ordered = 0;
    for (const auto& l : kLooks) {
        for (size_t a = 0; a < app_count && ordered < kMaxRows; a++) {
            if (apps[a].name && strcmp(apps[a].name, l.app) == 0) order[ordered++] = a;
        }
    }
    for (size_t a = 0; a < app_count && ordered < kMaxRows; a++) {
        if (!look_for(apps[a].name)) order[ordered++] = a;
    }
    if (app_count > kMaxRows) {
        LV_LOG_WARN("launcher menu: %u apps, only %u rows shown",
                    (unsigned)app_count, (unsigned)kMaxRows);
    }

    const char* prefix = getGuiPlatform().assetPathPrefix();
    for (size_t o = 0; o < ordered; o++) {
        if (apps[order[o]].app_ptr) add_row(apps[order[o]], prefix);
    }

    if (s_rowCount == 0) return wrap;
    if (want >= s_rowCount) want = 0;
    if (s_config.encoder_group && s_sel != want) {
        lv_group_focus_obj(s_rows[want].row);   // its FOCUSED expands it
    }
    if (s_sel != want) select_row(want);
    refresh_header();   // the count was still growing when row 0 was focused
    s_focusedByPointer = false;
    return wrap;
}

} // namespace crosspad_gui

#endif // USE_LVGL
