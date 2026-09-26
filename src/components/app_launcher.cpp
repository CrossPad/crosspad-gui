#ifdef USE_LVGL

#include "crosspad-gui/components/app_launcher.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad/app/AppRegistry.hpp"
#include "crosspad/kit/IKitManager.hpp"
#include "crosspad/kit/KitInfo.hpp"
#include "crosspad/platform/PlatformServices.hpp"
#include <cstring>
#include <cstdio>

namespace crosspad_gui {

// ── Internal state for callbacks ────────────────────────────────────────────

// Stored config copy for the current launcher (set by launcher_create_menu).
// Copied by value to avoid dangling pointer if caller passes a stack-local config.
static LauncherConfig s_config = {};

// ── Power OFF dialog ────────────────────────────────────────────────────────

// Context for ClosePopUpApp callback
struct PopupCtx {
    void* app_ptr;
    launcher_close_popup_cb_t on_close;
};

// Encoder group state for the active power off dialog
static lv_group_t* s_powerOffGroup = nullptr;
static lv_group_t* s_powerOffPrevGroup = nullptr;

static void RestorePowerOffEncoder() {
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav && s_powerOffPrevGroup) {
        lv_indev_restore_group(nav, s_powerOffPrevGroup);
    }
    if (s_powerOffGroup) {
        lv_group_delete(s_powerOffGroup);
        s_powerOffGroup = nullptr;
    }
    s_powerOffPrevGroup = nullptr;
}

static void ClosePowerOffMsgboxEvent(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        RestorePowerOffEncoder();
    }
}

static void ClosePopUpApp(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    LV_LOG_INFO("Power off cancelled");
    auto* ctx = (PopupCtx*)lv_event_get_user_data(e);
    if (ctx && ctx->on_close) {
        ctx->on_close(ctx->app_ptr);
    }
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
    lv_obj_t* mbox = lv_obj_get_parent(lv_obj_get_parent(btn));
    lv_msgbox_close(mbox);
    // Note: ctx is leaked (small, one-shot allocation per dialog)
}

lv_obj_t* launcher_create_power_off_dialog(
    lv_obj_t* parent,
    void* app_ptr,
    launcher_close_popup_cb_t on_close)
{
    lv_obj_t* msgbox = lv_msgbox_create(parent);
    lv_obj_t* closeBtn = lv_msgbox_add_close_button(msgbox);
    lv_obj_remove_event(closeBtn, 0);  // remove default close event
    lv_msgbox_add_title(msgbox, "Power OFF");
    lv_msgbox_add_text(msgbox, "Are you sure you want to power off?");
    lv_obj_t* yes = lv_msgbox_add_footer_button(msgbox, "Yes");
    lv_obj_t* no  = lv_msgbox_add_footer_button(msgbox, "No");
    lv_obj_add_style(yes, &styleButton, 0);
    lv_obj_add_style(no, &styleButton, 0);

    // "Yes" sends power off via platform
    lv_obj_add_event_cb(yes, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

        LV_LOG_INFO("Powering off...");
        lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
        lv_obj_t* footer = lv_obj_get_parent(obj);
        lv_obj_clean(footer);
        lv_obj_t* label = lv_label_create(footer);
        lv_label_set_text(label, "Powering off...");
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

        lv_async_call([](void*) {
            auto& platform = getGuiPlatform();
            platform.sendPowerOff();
            platform.delayMs(200);
            LV_LOG_WARN("Powering off...");
            platform.sendPowerOff();
            platform.delayMs(500);
            platform.sendPowerOff();
        }, nullptr);
    }, LV_EVENT_CLICKED, msgbox);

    // "No" and close button dismiss
    auto* ctx = new PopupCtx{app_ptr, on_close};
    lv_obj_add_event_cb(no, ClosePopUpApp, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(closeBtn, ClosePopUpApp, LV_EVENT_CLICKED, ctx);

    // Route encoder to msgbox buttons; restore previous group on delete
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    if (nav) {
        s_powerOffPrevGroup = lv_indev_get_group(nav);
        s_powerOffGroup = lv_group_create();
        lv_group_add_obj(s_powerOffGroup, no);
        lv_group_add_obj(s_powerOffGroup, yes);
        lv_group_add_obj(s_powerOffGroup, closeBtn);
        lv_group_focus_obj(no);
        lv_indev_set_group(nav, s_powerOffGroup);
        lv_obj_add_event_cb(msgbox, ClosePowerOffMsgboxEvent, LV_EVENT_DELETE, nullptr);
    }

    return msgbox;
}

// ── Power OFF app registration ──────────────────────────────────────────────

// Internal: LVGL factory for Power OFF dialog, adapts to launcher_create_power_off_dialog
static lv_obj_t* PowerOffFactory(lv_obj_t* parent, App* a) {
    // We use on_close from the stored config if available
    launcher_close_popup_cb_t on_close = s_config.on_close;
    return launcher_create_power_off_dialog(parent, (void*)a, on_close);
}

void launcher_register_power_off() {
    auto& registry = crosspad::AppRegistry::getInstance();

    // Build icon path using platform asset prefix
    static char icon_path[64];
    snprintf(icon_path, sizeof(icon_path), "%spower.png",
             getGuiPlatform().assetPathPrefix());

    registry.registerApp({
        "Power OFF",
        icon_path,
        PowerOffFactory,
        nullptr,  // no destroy
        -100      // keep the destructive Power OFF action at the end of the launcher
    });
}

// ── Menu ────────────────────────────────────────────────────────────────────
//
// The "magnifier" list: every app is a slim row, the one under the encoder
// grows into a lime card with its icon in a chip, its name and a line saying
// what it does. Turning the encoder moves the card; pressing opens it. On the
// touch screen the first tap selects a row and a tap on the selected one opens
// it, so a finger can look before it commits.
//
// Geometry and palette are the ones in the design: collapsed rows 18 px
// (radius 6), the selected one 42 px (radius 13), 3 px apart, list padded
// 9 x 6; icon 12 px, 16 px in a 26 px chip when selected.

namespace {

constexpr int32_t kRowH       = 18;
constexpr int32_t kRowSelH    = 42;
constexpr int32_t kRowGap     = 3;
constexpr int32_t kListPadH   = 9;
constexpr int32_t kListPadV   = 6;
constexpr int32_t kHeaderH    = 18;
constexpr int32_t kIcon       = 12;
constexpr int32_t kIconSel    = 16;
constexpr int32_t kChipSel    = 26;
constexpr size_t  kMaxRows    = 24;

const uint32_t kBg       = 0x16181B;
const uint32_t kRow      = 0x20242A;
const uint32_t kAccent   = 0xC6FF47;
const uint32_t kInk      = 0x16181B;
const uint32_t kLabelDim = 0x9AA0A8;
const uint32_t kBarText  = 0xE8EAED;
const uint32_t kBarMeta  = 0x7B828B;

/// How an app is shown in the menu, keyed by the name it registers under.
///
/// THE ORDER OF THIS TABLE IS THE ORDER OF THE MENU -- move a line to move the
/// row. An app not listed here goes after all of these, in registry priority
/// order, with its own name and icon and a neutral colour.
struct MenuLook {
    const char* app;
    const char* label;
    uint32_t    color;
    const char* meta;
    const char* icon;   // asset file name, or null for the app's own icon
};

const MenuLook kLooks[] = {
    {"KitSelector",  "Kits",         0x5AC8FF, "browse and load a kit",     "ic_kits.png"},
    {"Sampler",      "Sampler",      0xC6FF47, "play the kit on the pads",  "ic_sampler.png"},
    {"Sequencer",    "Sequencer",    0xFF7A9A, "record and loop the pads",  "ic_sequencer.png"},
    {"Song",         "Song",         0xF26DF9, "scenes arranged in a row",  "ic_song.png"},
    {"Arrange",      "Arrange",      0x4FC3F7, "audio tracks on a timeline", "ic_arrange.png"},
    {"PadMixer",     "Pad Mixer",    0x7AA2FF, "a fader for every pad",     nullptr},
    {"Mixer",        "Mixer",        0xFFC857, "channels and outputs",      "ic_mixer.png"},
    {"DawControl",   "DAW Control",  0xA0E7A0, "FL Studio, Ableton",        nullptr},
    {"Update",       "Update",       0xFF9552, "firmware update",           "ic_update.png"},
    {"Instructions", "Instructions", 0x4FE0B0, "shortcuts and diagrams",    "ic_help.png"},
    {"Settings",     "Settings",     0xB48CFF, "audio, MIDI, display",      "ic_settings.png"},
    {"Power OFF",    "Power",        0xE8EAED, "shut down",                 "ic_power.png"},
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
};

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
    lv_style_set_radius(&s_stRow, 6);
    lv_style_set_border_width(&s_stRow, 0);
    lv_style_set_pad_hor(&s_stRow, 9);
    lv_style_set_pad_ver(&s_stRow, 0);
    lv_style_set_pad_column(&s_stRow, 8);

    lv_style_init(&s_stRowSel);
    lv_style_set_bg_color(&s_stRowSel, lv_color_hex(kAccent));
    lv_style_set_radius(&s_stRowSel, 13);
    lv_style_set_pad_hor(&s_stRowSel, 8);

    lv_style_init(&s_stChipSel);
    lv_style_set_bg_color(&s_stChipSel, lv_color_hex(kInk));
    lv_style_set_bg_opa(&s_stChipSel, 36);   // ~14 %
    lv_style_set_radius(&s_stChipSel, 9);
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
        lv_obj_set_style_image_recolor(r.icon, ic, 0);
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
    s_focusedByPointer = indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER;
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

void row_deleted_cb(lv_event_t* e) {
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
    lv_obj_add_event_cb(wrap, row_deleted_cb, LV_EVENT_DELETE, nullptr);

    /* -- header: the selected app's colour, the kit, and where you are -- */
    lv_obj_t* bar = make_box(wrap);
    lv_obj_set_size(bar, LV_PCT(100), kHeaderH);
    lv_obj_set_style_pad_hor(bar, kListPadH, 0);
    lv_obj_set_style_pad_top(bar, 4, 0);

    s_barMark = make_box(bar);
    lv_obj_set_size(s_barMark, 3, 12);
    lv_obj_set_style_radius(s_barMark, 2, 0);
    lv_obj_set_style_bg_opa(s_barMark, LV_OPA_COVER, 0);
    lv_obj_align(s_barMark, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* kit = lv_label_create(bar);
    lv_obj_set_style_text_font(kit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(kit, lv_color_hex(kBarText), 0);
    lv_label_set_long_mode(kit, LV_LABEL_LONG_DOT);
    lv_obj_set_width(kit, 220);
    crosspad::IKitManager* km = crosspad::getKitManager();
    crosspad::KitInfo* ki = km ? km->getCurrentKit() : nullptr;
    lv_label_set_text(kit, ki ? ki->name.c_str() : "No kit");
    lv_obj_align(kit, LV_ALIGN_LEFT_MID, 10, 0);

    s_barPos = lv_label_create(bar);
    lv_obj_set_style_text_font(s_barPos, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_barPos, lv_color_hex(kBarMeta), 0);
    lv_obj_align(s_barPos, LV_ALIGN_RIGHT_MID, 0, 0);

    /* -- the list. Scrolls when there are more apps than fit; the encoder
     *    keeps the card in view, the scrollbar would only take width. -- */
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

    const char* prefix = getGuiPlatform().assetPathPrefix();
    static char iconPaths[kMaxRows][48];

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

    for (size_t o = 0; o < ordered; o++) {
        const auto& info = apps[order[o]];
        if (!info.app_ptr) continue;
        const size_t i = s_rowCount++;
        Row& r = s_rows[i];
        r = Row{};
        r.app = info.app_ptr;

        const MenuLook* look = look_for(info.name);
        r.color = look ? look->color : kBarText;

        const char* iconSrc = info.icon;
        if (look && look->icon) {
            snprintf(iconPaths[i], sizeof(iconPaths[i]), "%s%s", prefix, look->icon);
            iconSrc = iconPaths[i];
        }
        r.glyph = iconSrc && (uint8_t)iconSrc[0] >= 0x80;

        r.row = make_box(s_list);
        lv_obj_add_style(r.row, &s_stRow, 0);
        lv_obj_set_size(r.row, LV_PCT(100), kRowH);
        lv_obj_set_flex_flow(r.row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(r.row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r.row, row_focused_cb, LV_EVENT_FOCUSED, (void*)(uintptr_t)i);
        lv_obj_add_event_cb(r.row, row_clicked_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        r.chip = make_box(r.row);
        if (r.glyph) {
            r.icon = lv_label_create(r.chip);
            lv_label_set_text(r.icon, iconSrc);
        } else {
            r.icon = lv_image_create(r.chip);
            if (iconSrc) lv_image_set_src(r.icon, iconSrc);
            lv_image_set_inner_align(r.icon, LV_IMAGE_ALIGN_CONTAIN);
            lv_obj_set_style_image_recolor_opa(r.icon, LV_OPA_COVER, 0);
        }

        lv_obj_t* txt = make_box(r.row);
        lv_obj_set_flex_grow(txt, 1);
        lv_obj_set_height(txt, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(txt, 1, 0);

        r.name = lv_label_create(txt);
        lv_label_set_long_mode(r.name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(r.name, LV_PCT(100));
        lv_label_set_text(r.name, look ? look->label : info.name);

        r.meta = lv_label_create(txt);
        lv_obj_set_style_text_font(r.meta, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(r.meta, lv_color_hex(kInk), 0);
        lv_obj_set_style_text_opa(r.meta, 179, 0);   // ~70 %
        lv_label_set_long_mode(r.meta, LV_LABEL_LONG_DOT);
        lv_obj_set_width(r.meta, LV_PCT(100));
        lv_label_set_text(r.meta, look ? look->meta : "");

        apply(i, false);
        /* Last: the group focuses its first member as it is added, and that
         * FOCUSED expands a row -- which needs its children to exist. */
        if (s_config.encoder_group) lv_group_add_obj(s_config.encoder_group, r.row);
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
