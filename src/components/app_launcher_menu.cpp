#ifdef USE_LVGL

#include "crosspad-gui/components/app_launcher.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad/kit/IKitManager.hpp"
#include "crosspad/kit/KitInfo.hpp"
#include "crosspad/platform/PlatformServices.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace crosspad_gui {

// ── Menu ────────────────────────────────────────────────────────────────────
//
// The app list in the look of the Settings menu: every app a row of its own
// height, the theme's accent down its left edge, its icon in the accent, its
// name, and a chevron -- the same row styles Settings uses, so the two stay
// one design. The encoder moves the focus, a press or a tap opens the row.
// Above the list, the loaded kit and where the focus is.

namespace {

// Geometry of the design; the rows' own come from styleSettingsRow.
constexpr int32_t kRowH          = 36;
constexpr int32_t kRowGap        = 4;
constexpr int32_t kRowIconGap    = 8;
constexpr int32_t kIcon          = 16;
constexpr int32_t kListPad       = 6;
constexpr int32_t kHeaderH       = 18;
constexpr int32_t kHeaderPadTop  = 4;
constexpr int32_t kHeaderMarkW   = 3;
constexpr int32_t kHeaderMarkH   = 12;
constexpr int32_t kHeaderMarkR   = 2;
constexpr int32_t kHeaderKitX    = 10;
constexpr int32_t kHeaderKitW    = 220;
/// Rows the menu holds; the registry holds 16 apps, plus the built-in Power.
constexpr size_t  kMaxRows       = 24;
/// An icon path: the platform's asset prefix (a whole absolute directory on
/// the simulator) and the file name.
constexpr size_t  kIconPathLen   = 256;

constexpr uint32_t kBarText  = 0xE8EAED;
constexpr uint32_t kBarMeta  = 0x7B828B;
const lv_color_t   kChevron  = lv_color_make(100, 100, 100);   // as the Settings rows'

/// How an app is shown in the menu, keyed by the name it registers under.
///
/// THE ORDER OF THIS TABLE IS THE ORDER OF THE MENU -- move a line to move the
/// row. An app not listed here goes after all of these, in registry priority
/// order, with its own name and icon.
///
/// The ic_*.png icons are 16 px on purpose: a 64 px app icon drawn at 16 px
/// still decodes at 64 px into LVGL's image cache, sixteen times the PSRAM.
struct MenuLook {
    const char* app;
    const char* label;
    const char* icon;   // asset file name, or null for the app's own icon
};

const MenuLook kLooks[] = {
    {"KitSelector",  "Kits",         "ic_kits.png"},
    {"Sampler",      "Sampler",      "ic_sampler.png"},
    {"Sequencer",    "Sequencer",    "ic_sequencer.png"},
    {"Song",         "Song",         "ic_song.png"},
    {"Arrange",      "Arrange",      "ic_arrange.png"},
    {"Recorder",     "Recorder",     "ic_recorder.png"},
    {"Slicer",       "Slicer",       "ic_slicer.png"},
    {"PitchPlay",    "Pitch Play",   "ic_rompler.png"},
    {"PadMixer",     "Pad Mixer",    nullptr},
    {"Mixer",        "Mixer",        "ic_mixer.png"},
    {"DawControl",   "DAW Control",  nullptr},
    {"Firmware",     "Firmware",     "ic_update.png"},
    {"Instructions", "Instructions", "ic_help.png"},
    {"Settings",     "Settings",     "ic_settings.png"},
    {"Power OFF",    "Power",        "ic_power.png"},
};

const MenuLook* look_for(const char* name) {
    for (const auto& l : kLooks) {
        if (name && strcmp(l.app, name) == 0) return &l;
    }
    return nullptr;
}

struct Row {
    lv_obj_t* row = nullptr;
    void*     app = nullptr;
};

LauncherConfig s_config = {};
Row      s_rows[kMaxRows];
size_t   s_rowCount = 0;
/* Survives a rebuild: coming back from an app lands on the row it was
 * opened from. */
size_t   s_sel = 0;
lv_obj_t* s_list = nullptr;
lv_obj_t* s_appContainer = nullptr;
lv_obj_t* s_barPos = nullptr;

void refresh_header() {
    if (s_barPos) lv_label_set_text_fmt(s_barPos, "%u / %u",
                                        (unsigned)(s_sel + 1), (unsigned)s_rowCount);
}

/* Scrolled to at once: LVGL's own scroll-on-focus animates the list, and
 * every frame of that redraws all of it (the Settings list measured 3-4 full
 * screens per encoder step). */
void row_focused_cb(lv_event_t* e) {
    s_sel = (size_t)(uintptr_t)lv_event_get_user_data(e);
    refresh_header();
    lv_obj_scroll_to_view(lv_event_get_target_obj(e), LV_ANIM_OFF);
}

void row_clicked_cb(lv_event_t* e) {
    const size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (i < s_rowCount && s_rows[i].app && s_config.on_select) {
        s_config.on_select(s_rows[i].app, s_appContainer);
    }
}

void menu_deleted_cb(lv_event_t* e) {
    /* The launcher is torn down by lv_obj_clean() of the whole screen; the
     * table must not outlive the rows it points at. */
    (void)e;
    s_rowCount = 0;
    s_list = s_barPos = s_appContainer = nullptr;
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
    lv_obj_set_style_pad_hor(bar, kListPad, 0);
    lv_obj_set_style_pad_top(bar, kHeaderPadTop, 0);

    lv_obj_t* mark = make_box(bar);
    lv_obj_set_size(mark, kHeaderMarkW, kHeaderMarkH);
    lv_obj_set_style_radius(mark, kHeaderMarkR, 0);
    lv_obj_set_style_bg_color(mark, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_align(mark, LV_ALIGN_LEFT_MID, 0, 0);

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

    /* lv_image_set_src() keeps its own copy of a file path, so the buffer
     * only has to outlive this call. */
    char iconPath[kIconPathLen];
    const char* iconSrc = info.icon;
    if (look && look->icon) {
        snprintf(iconPath, sizeof(iconPath), "%s%s", prefix, look->icon);
        iconSrc = iconPath;
    }

    /* No theme: its transition on every state change costs extra full
     * redraws of two rows per focus step (as in Settings). */
    r.row = make_box(s_list);
    lv_obj_add_style(r.row, &styleSettingsRow, 0);
    lv_obj_add_style(r.row, &styleSettingsRowFocused, LV_STATE_FOCUSED);
    lv_obj_set_size(r.row, LV_PCT(100), kRowH);
    lv_obj_set_flex_flow(r.row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r.row, kRowIconGap, 0);
    lv_obj_add_flag(r.row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(r.row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(r.row, row_focused_cb, LV_EVENT_FOCUSED, (void*)(uintptr_t)i);
    lv_obj_add_event_cb(r.row, row_clicked_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

    /* The row is what a tap lands on: nothing in it takes the press. Every
     * icon in the accent, as in Settings -- an app's own icon too, as its
     * silhouette. */
    if (iconSrc && (uint8_t)iconSrc[0] >= 0x80) {
        lv_obj_t* glyph = lv_label_create(r.row);
        lv_label_set_text(glyph, iconSrc);
        lv_obj_set_style_text_font(glyph, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(glyph, CROSSPAD_THEME_PRIMARY, 0);
    } else {
        lv_obj_t* icon = lv_image_create(r.row);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(icon, kIcon, kIcon);
        if (iconSrc) lv_image_set_src(icon, iconSrc);
        lv_image_set_inner_align(icon, LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_set_style_image_recolor(icon, CROSSPAD_THEME_PRIMARY, 0);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    }

    lv_obj_t* name = lv_label_create(r.row);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_text(name, look ? look->label : info.name);

    lv_obj_t* chev = lv_label_create(r.row);
    lv_obj_set_style_text_color(chev, kChevron, 0);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);

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
    s_appContainer = parent;
    s_rowCount = 0;
    const size_t want = s_sel;   // the row to come back to

    lv_obj_t* wrap = make_box(parent);
    lv_obj_set_size(wrap, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wrap, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(wrap, menu_deleted_cb, LV_EVENT_DELETE, nullptr);

    build_header(wrap);

    /* The list scrolls when there are more apps than fit; the encoder keeps
     * the focus in view, a scrollbar would only take width. */
    s_list = make_box(wrap);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_list, kListPad, 0);
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
    s_sel = want < s_rowCount ? want : 0;
    /* Laid out first, or the scroll to a row further down goes nowhere. */
    lv_obj_update_layout(wrap);
    if (s_config.encoder_group) lv_group_focus_obj(s_rows[s_sel].row);
    lv_obj_scroll_to_view(s_rows[s_sel].row, LV_ANIM_OFF);
    refresh_header();
    return wrap;
}

} // namespace crosspad_gui

#endif // USE_LVGL
