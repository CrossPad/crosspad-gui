// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/mode_overlay.h"
#include "crosspad-gui/components/mode_more.h"
#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/components/power_gesture.h"
#include "crosspad-gui/components/transport_bar.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad/pad/IPadLogicHandler.hpp"
#include "crosspad/song/ArrangeFocus.hpp"
#include "crosspad/pad/PadManager.hpp"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "crosspad/settings/ISettingsUI.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace crosspad_gui {

namespace {

constexpr const char* kPadLogicName = "ModeOverlay";
constexpr int kPads = 16;

const lv_color_t kBg    = lv_color_hex(0x16181B);
const lv_color_t kCell  = lv_color_hex(0x20242A);
const lv_color_t kText  = lv_color_hex(0xE8EAED);
const lv_color_t kDim   = lv_color_hex(0x7B828B);

/* 0 = the theme colour, so the core row follows Settings > Display > Theme. */
constexpr uint32_t kTheme = 0;

const ModeEntry kDefaultMap[] = {
    // pad, label,      hint,        colour,   kind,            app
    {  1, "Sampler",  "play",  kTheme,   ModeKind::Core,  "Sampler",      nullptr, nullptr },
    {  2, "Seq",      "record",  kTheme,   ModeKind::Core,  "Sequencer",    nullptr, nullptr },
    {  3, "Song",     "blocks",     kTheme,   ModeKind::Core,  "Song",         nullptr, nullptr },
    {  4, "Arrange",  "4 tracks",   kTheme,   ModeKind::Core,  "Arrange",      nullptr, nullptr },
    {  5, "Record",   "mic/line",  0xFF5C5C, ModeKind::App,   "Recorder",     nullptr, nullptr },
    {  6, "Slicer",   "chop",       0xF5C46A, ModeKind::App,   "Slicer",       nullptr, nullptr },
    {  8, "More",     "apps",       0x7B828B, ModeKind::More,  nullptr,        nullptr, nullptr },
    {  9, "Pitch",    "on keys",    0xA77BF0, ModeKind::App,   "PitchPlay",    nullptr, nullptr },
    { 10, "Pad Mix",  "faders",  0x4FE0B0, ModeKind::App,   "PadMixer",     nullptr, nullptr },
    { 11, "Mixer",    "routing",    0x4FE0B0, ModeKind::App,   "Mixer",        nullptr, nullptr },
    { 12, "DAW",      "host",       0x4FC3F7, ModeKind::App,   "DawControl",   nullptr, nullptr },
    { 13, "Kits",     "browse",     0xC6FF47, ModeKind::Rare,  "KitSelector",  nullptr, nullptr },
    { 14, "Help",     "manual",     0x7B828B, ModeKind::Rare,  "Instructions", nullptr, nullptr },
    { 15, "Update",   "firmware",     0x7B828B, ModeKind::Rare,  "Firmware",     nullptr, nullptr },
    { 16, "Setup",    "audio",      0x7B828B, ModeKind::Rare,  "Settings",     nullptr, nullptr },
};

struct State {
    std::array<ModeEntry, kPads> map{};   // indexed by pad - 1; label == nullptr = empty
    lv_obj_t* appContainer = nullptr;
    /// What the home screen is (Settings -> Display -> Home screen), as the
    /// main screen was built with it.
    crosspad::LauncherStyle homeStyle = crosspad::LauncherStyle::Pads;
    bool registered = false;

    /// The map as the home screen: in the app container under the apps,
    /// built by build_main_screen() for the Pads style. Its cells are in the
    /// main screen's encoder group, like a launcher's icons.
    lv_obj_t* home = nullptr;
    std::array<lv_obj_t*, kPads> homeCells{};

    /// The map over an app: what MODE opens. Its own encoder group, and the
    /// one it took the encoder from.
    lv_obj_t* overlay = nullptr;
    std::array<lv_obj_t*, kPads> overlayCells{};
    lv_group_t* group = nullptr;
    lv_group_t* priorGroup = nullptr;
    uint32_t backToken = 0;

    /// "More" over whichever map it was opened from, with an encoder group
    /// of its own: the home map's group is the screen's, which also holds the
    /// transport keys, and is not the list's to empty.
    lv_obj_t* more = nullptr;
    lv_group_t* moreGroup = nullptr;
    lv_group_t* morePriorGroup = nullptr;
    uint32_t moreBackToken = 0;

    /// The map's pad logic is on PadManager's stack.
    bool padsPushed = false;
    bool checkPending = false;
};

State s;
/* Read from the pad and MIDI tasks (mode_overlay_is_open()), so not a member
 * of the LVGL-only state above. True while a map is on screen with nothing
 * over it: the pads choose rather than play. */
std::atomic<bool> s_open{false};

/// Case-insensitive equality without <strings.h>, which MSVC (crosspad-pc) lacks.
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

lv_color_t entry_color(const ModeEntry& e)
{
    return e.color == kTheme ? CROSSPAD_THEME_PRIMARY : lv_color_hex(e.color);
}

std::shared_ptr<ILvglApp> find_app(const char* name)
{
    if (!name) return nullptr;
    for (const auto& app : AppOrchestrator::getInstance().getApps()) {
        if (app && same_name(app->getName(), name)) return app;
    }
    return nullptr;
}

/// The entry on @p pad, or nullptr when the pad is free -- or names an app
/// this firmware does not have: a cell that opens nothing is a dead pad.
const ModeEntry* entry_for_pad(uint8_t pad)
{
    if (pad < 1 || pad > kPads) return nullptr;
    const ModeEntry& e = s.map[pad - 1];
    if (!e.label) return nullptr;
    if (e.app && !find_app(e.app)) return nullptr;
    return &e;
}

ILvglApp* foreground()
{
    return AppOrchestrator::getInstance().getForegroundApp();
}

bool is_running(const ModeEntry& e)
{
    ILvglApp* run = foreground();
    return run && e.app && same_name(run->getName(), e.app);
}

bool map_is_home() { return s.homeStyle == crosspad::LauncherStyle::Pads; }

/// The home map is the screen when it exists and nothing is over it.
bool home_in_charge() { return s.home && !s.overlay && !foreground(); }

void update_open() { s_open.store(s.overlay != nullptr || home_in_charge()); }

// ── Pads ───────────────────────────────────────────────────────────────────

class ModePadLogic final : public crosspad::IPadLogicHandler {
public:
    void onActivate(crosspad::PadManager& pm) override
    {
        for (uint8_t i = 0; i < kPads; ++i) {
            const ModeEntry* e = entry_for_pad(i + 1);
            if (!e) { pm.setPadColor(i, 0, 0, 0); continue; }
            const lv_color32_t c = lv_color_to_32(entry_color(*e), LV_OPA_COVER);
            /* Toggles that are off and the rare row glow at half: the eye
             * finds the four core pads first. */
            const bool dim = (e->kind == ModeKind::Rare) || (e->kind == ModeKind::More) ||
                             (e->kind == ModeKind::Toggle && !(e->isOn && e->isOn()));
            const uint8_t k = dim ? 2 : 1;
            pm.setPadColor(i, c.red / k, c.green / k, c.blue / k);
        }
    }
    void onDeactivate(crosspad::PadManager& pm) override
    {
        for (uint8_t i = 0; i < kPads; ++i) pm.clearPadColor(i);
    }
    void onPadPress(crosspad::PadManager& pm, uint8_t padIdx, uint8_t) override
    {
        /* Only a finger chooses. The sequencer keeps pressing pads while the
         * map is up -- the bar stays live -- and a host's MIDI notes arrive
         * here too; either one used to pick an app on its own. */
        if (pm.pressSource(padIdx) != crosspad::EventSource::PadManager) return;
        mode_overlay_select_pad(padIdx);   // pad task -> LVGL task
    }
    void onPadRelease(crosspad::PadManager&, uint8_t) override {}
    void onPadPressure(crosspad::PadManager&, uint8_t, uint8_t) override {}
};

void take_pads()
{
    if (!s.registered || s.padsPushed) return;
    crosspad::getPadManager().pushPadLogic(kPadLogicName);
    s.padsPushed = true;
}

/// Hand the pads back. Only while the map's logic is the active one: an app
/// started some way that bypasses the orchestrator has pushed its own over
/// ours, and popping now would pop the app's -- its own destroyApp() pops
/// it and leaves ours on top again, still ours.
void release_pads()
{
    if (!s.padsPushed) return;
    auto& pm = crosspad::getPadManager();
    if (pm.getActivePadLogic() != kPadLogicName) return;
    pm.popPadLogic();
    s.padsPushed = false;
}

// ── Acting on a choice ─────────────────────────────────────────────────────

void paint_cells(const std::array<lv_obj_t*, kPads>& cells);
void open_over_app();
void schedule_check();
void open_more(lv_obj_t* over);
void close_more();

void launch(const char* name)
{
    /* A pad chosen here is a player leaving whatever flow they were in, so
     * the Arrange track one app was filling is no longer being filled. Only
     * on this path: mode_overlay_open_app() is also how Arrange hands over,
     * and that hand-over is exactly what sets the focus. The orchestrator's
     * before-launch hook closes the map and hands the pads back. */
    crosspad::arrangeFocus().clear();
    mode_overlay_open_app(name);
    schedule_check();
}

void choose(uint8_t pad)
{
    const ModeEntry* e = entry_for_pad(pad);
    if (!e) return;

    switch (e->kind) {
        case ModeKind::Toggle:
            if (e->toggle) e->toggle();
            if (s.overlay) mode_overlay_close();
            else paint_cells(s.homeCells);
            return;
        case ModeKind::More:
            open_more(s.overlay ? s.overlay : s.home);
            return;
        default:
            break;
    }
    if (is_running(*e)) {   // MODE over the app it names: back to it
        mode_overlay_close();
        return;
    }
    launch(e->app);
}

void on_cell_clicked(lv_event_t* ev)
{
    const auto pad = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(ev)));
    choose(pad);
}

void async_select(void* p)
{
    const auto idx = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(p));
    if (mode_overlay_is_open()) choose(idx + 1);
}

/// Power click with the map over an app: back to the app.
bool on_back(void*)
{
    if (!s.overlay) return false;
    mode_overlay_close();
    return true;
}

/// What the screen is after an app started or stopped. At home on the map
/// the pads choose; under an app they are the app's. A Home screen style
/// changed in Settings while an app was open takes effect here, on the way
/// back -- the main screen is rebuilt in the new style.
void check_cb(void*)
{
    s.checkPending = false;
    if (!s.appContainer) return;
    if (!foreground()) {
        auto* cfg = crosspad::CrosspadSettings::getInstance();
        if (cfg && cfg->launcherStyle != s.homeStyle) {
            if (auto* ui = crosspad::getSettingsUI()) { ui->exitToHome(); return; }
        }
    }
    if (home_in_charge()) take_pads();
    else if (!s.overlay)  release_pads();
    if (s.home) paint_cells(s.homeCells);
    update_open();
}

/// Every app start goes through the orchestrator, and this runs first there:
/// the map's pad logic must be off the stack before the app under the map
/// pops its own and the next one pushes its own.
void close_before_launch()
{
    close_more();
    if (s.overlay) mode_overlay_close();
    release_pads();
    s_open.store(false);
}

/// Runs on the LVGL task (or under the display lock): deferred, because the
/// orchestrator calls it from inside start() and destroyApp().
void schedule_check()
{
    if (s.checkPending) return;
    s.checkPending = true;
    lv_async_call(check_cb, nullptr);
}

// ── Drawing ────────────────────────────────────────────────────────────────

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t col, const char* text)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_label_set_text(l, text);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

lv_obj_t* build_cell(lv_obj_t* parent, const ModeEntry& e, int32_t x, int32_t y, int32_t w, int32_t h)
{
    const lv_color_t col = entry_color(e);
    const bool toggle = e.kind == ModeKind::Toggle;

    lv_obj_t* c = lv_button_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_radius(c, 5, 0);
    lv_obj_set_style_bg_opa(c, toggle ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, kCell, 0);
    lv_obj_set_style_bg_color(c, lv_color_lighten(kCell, 40), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(c, col, 0);
    lv_obj_set_style_border_width(c, toggle ? 1 : 0, 0);
    lv_obj_set_style_outline_color(c, lv_color_white(), 0);
    lv_obj_set_style_outline_width(c, 0, 0);
    lv_obj_set_style_outline_pad(c, 0, 0);
    lv_obj_set_style_border_color(c, kText, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(c, 2, LV_STATE_FOCUS_KEY);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(c, on_cell_clicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(e.pad)));

    if (!toggle) {
        lv_obj_t* stripe = lv_obj_create(c);
        lv_obj_remove_style_all(stripe);
        lv_obj_set_size(stripe, 4, h);
        lv_obj_set_style_radius(stripe, 2, 0);
        lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(stripe, col, 0);
        lv_obj_remove_flag(stripe, LV_OBJ_FLAG_CLICKABLE);
    }

    /* Name on top; the hint and the pad number share the bottom line. */
    const lv_color_t labelCol = (e.kind == ModeKind::Rare) ? kText : col;
    lv_obj_t* l = label(c, &lv_font_montserrat_14, labelCol, e.label);
    lv_obj_set_width(l, w - 12);
    lv_obj_set_pos(l, 9, 4);

    if (e.hint && *e.hint && h >= 36) {
        lv_obj_t* hl = label(c, &lv_font_montserrat_10, kDim, e.hint);
        lv_obj_set_width(hl, w - 26);
        lv_obj_set_pos(hl, 9, h - 15);
    }

    char num[4];
    lv_snprintf(num, sizeof num, "%u", static_cast<unsigned>(e.pad));
    lv_obj_t* n = label(c, &lv_font_montserrat_10, kDim, num);
    lv_obj_align(n, LV_ALIGN_BOTTOM_RIGHT, -5, -3);
    return c;
}

void paint_cells(const std::array<lv_obj_t*, kPads>& cells)
{
    for (int i = 0; i < kPads; ++i) {
        lv_obj_t* c = cells[i];
        const ModeEntry* e = entry_for_pad(i + 1);
        if (!c || !e) continue;
        lv_obj_set_style_outline_width(c, is_running(*e) ? 2 : 0, 0);
        if (e->kind == ModeKind::Toggle) {
            const bool on = e->isOn && e->isOn();
            lv_obj_set_style_bg_opa(c, on ? LV_OPA_30 : LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_color(c, on ? entry_color(*e) : kCell, 0);
        }
    }
}

/// The map itself: a root of @p w x @p h in @p parent, the header and one
/// cell per pad that does something. Row 0 on screen is pads 13-16: the
/// screen is a picture of the pads.
lv_obj_t* build_map(lv_obj_t* parent, int32_t w, int32_t h, std::array<lv_obj_t*, kPads>& cells)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, kBg, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);   // swallow taps between cells

    lv_obj_t* head = label(o, &lv_font_montserrat_10, kDim, "MODE  -  hit a pad or tap");
    lv_obj_set_pos(head, 8, 4);

    constexpr int32_t kMargin = 4, kGap = 4, kHead = 18;
    const int32_t cw = (w - 2 * kMargin - 3 * kGap) / 4;
    const int32_t ch = (h - kHead - kMargin - 3 * kGap) / 4;

    cells.fill(nullptr);
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const uint8_t pad = static_cast<uint8_t>((3 - row) * 4 + col + 1);
            const ModeEntry* e = entry_for_pad(pad);
            if (!e) continue;
            cells[pad - 1] = build_cell(o, *e, kMargin + col * (cw + kGap),
                                        kHead + row * (ch + kGap), cw, ch);
        }
    }
    paint_cells(cells);
    return o;
}

/// Cells into @p g in pad order, the running app's (or pad 1's) focused.
void join_cells(lv_group_t* g, const std::array<lv_obj_t*, kPads>& cells)
{
    lv_obj_t* first = nullptr;
    for (int i = 0; i < kPads; ++i) {
        lv_obj_t* c = cells[i];
        if (!c) continue;
        lv_group_add_obj(g, c);
        const ModeEntry* e = entry_for_pad(i + 1);
        if (!first || (e && is_running(*e))) first = c;
    }
    if (first) lv_group_focus_obj(first);
}

/* Someone else wiped the screen under a map: the main screen's rebuild cleans
 * the home with the container, and the overlay layer can be cleared too. */
void on_home_delete(lv_event_t* ev)
{
    if (lv_event_get_target(ev) != s.home) return;
    if (s.more && !s.overlay) { s.more = nullptr; close_more(); }
    s.home = nullptr;
    s.homeCells.fill(nullptr);
    if (!s.overlay) release_pads();
    update_open();
}

void on_overlay_delete(lv_event_t* ev)
{
    if (lv_event_get_target(ev) != s.overlay) return;
    s.overlay = nullptr;
    s.overlayCells.fill(nullptr);
    if (s.more) { s.more = nullptr; close_more(); }
    update_open();
}

void open_over_app()
{
    if (s.overlay || !s.appContainer) return;
    /* Over the app's own area, wherever the platform put it -- below the
     * top bar, whatever that bar is. */
    lv_obj_update_layout(s.appContainer);
    lv_area_t a;
    lv_obj_get_coords(s.appContainer, &a);
    lv_obj_t* parent = getOverlayParent();
    s.overlay = build_map(parent, lv_area_get_width(&a), lv_area_get_height(&a), s.overlayCells);
    lv_obj_set_pos(s.overlay, a.x1, a.y1);
    lv_obj_add_event_cb(s.overlay, on_overlay_delete, LV_EVENT_DELETE, nullptr);
    take_pads();

    /* The encoder walks the cells in pad order and a click chooses; the app
     * under the map must not be driven blind. */
    if (lv_indev_t* enc = getGuiPlatform().getNavigationEncoder()) {
        s.group = lv_group_create();
        lv_group_set_wrap(s.group, true);
        join_cells(s.group, s.overlayCells);
        s.priorGroup = lv_indev_get_group(enc);
        lv_indev_set_group(enc, s.group);
    }
    s.backToken = ui_back_push(on_back, nullptr);
    update_open();
    transportbar_refresh();
}

/// A row of the More list chosen: the same path a pad of the map takes.
void pick_more(const char* name)
{
    close_more();
    launch(name);
}

/// Power click in the list: back to the map, not out of it.
bool on_more_back(void*)
{
    if (!s.more) return false;
    close_more();
    return true;
}

void open_more(lv_obj_t* over)
{
    if (!over || s.more) return;
    std::vector<const char*> registered;
    for (const auto& app : AppOrchestrator::getInstance().getApps()) {
        if (app) registered.push_back(app->getName());
    }
    std::vector<ModeEntry> map;
    for (const ModeEntry& e : s.map) if (e.label) map.push_back(e);
    const auto names = mode_more_apps(map.data(), map.size(), registered);

    lv_obj_update_layout(over);
    s.more = lv_obj_create(over);
    lv_obj_remove_style_all(s.more);
    lv_obj_set_size(s.more, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s.more, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s.more, kBg, 0);
    lv_obj_add_flag(s.more, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* head = label(s.more, &lv_font_montserrat_10, kDim, "MORE  -  every other app");
    lv_obj_set_pos(head, 8, 4);
    lv_obj_t* body = lv_obj_create(s.more);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, 0, 18);
    lv_obj_set_size(body, LV_PCT(100), lv_obj_get_height(over) - 18);

    if (lv_indev_t* enc = getGuiPlatform().getNavigationEncoder()) {
        s.moreGroup = lv_group_create();
        lv_group_set_wrap(s.moreGroup, true);
        s.morePriorGroup = lv_indev_get_group(enc);
        lv_indev_set_group(enc, s.moreGroup);
    }
    mode_more_build(body, names, pick_more, s.moreGroup);
    s.moreBackToken = ui_back_push(on_more_back, nullptr);
}

/// Also called after the list died with the map it was on (s.more already
/// null then): the back entry and the encoder still have to be handed back.
void close_more()
{
    ui_back_remove(s.moreBackToken);
    s.moreBackToken = 0;
    if (s.moreGroup) {
        lv_indev_t* enc = getGuiPlatform().getNavigationEncoder();
        if (enc && lv_indev_get_group(enc) == s.moreGroup) lv_indev_restore_group(enc, s.morePriorGroup);
        lv_group_delete_deferred(s.moreGroup);
        s.moreGroup = nullptr;
        s.morePriorGroup = nullptr;
    }
    if (s.more) {
        /* Closed from a row's CLICKED: hidden now, gone after. */
        lv_obj_add_flag(s.more, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(s.more);
        s.more = nullptr;
    }
}

void ensure_pad_logic_registered()
{
    if (s.registered) return;
    crosspad::getPadManager().registerPadLogic(kPadLogicName, std::make_shared<ModePadLogic>());
    s.registered = true;
}

} // namespace

// ── Public API ─────────────────────────────────────────────────────────────

const ModeEntry* mode_overlay_default_map(size_t* count)
{
    if (count) *count = sizeof(kDefaultMap) / sizeof(kDefaultMap[0]);
    return kDefaultMap;
}

void mode_overlay_init(lv_obj_t* app_container, const ModeEntry* entries, size_t count,
                       crosspad::LauncherStyle home)
{
    s.appContainer = app_container;
    s.homeStyle = home;
    s.map.fill(ModeEntry{});
    for (size_t i = 0; i < count; ++i) {
        const ModeEntry& e = entries[i];
        if (e.pad >= 1 && e.pad <= kPads && e.label) s.map[e.pad - 1] = e;
    }
    ensure_pad_logic_registered();
    transportbar_set_mode_handler(mode_overlay_toggle);
    AppOrchestrator::getInstance().setForegroundChangedHandler(schedule_check);
    AppOrchestrator::getInstance().setBeforeLaunchHandler(close_before_launch);
}

lv_obj_t* mode_overlay_create_home(lv_obj_t* parent, lv_group_t* group)
{
    if (!parent) return nullptr;
    lv_obj_update_layout(parent);
    s.home = build_map(parent, lv_obj_get_content_width(parent),
                       lv_obj_get_content_height(parent), s.homeCells);
    lv_obj_add_event_cb(s.home, on_home_delete, LV_EVENT_DELETE, nullptr);
    if (group) join_cells(group, s.homeCells);
    schedule_check();   // the pads are the map's once nothing is over it
    return s.home;
}

void mode_overlay_bind_toggle(uint8_t pad, void (*toggle)(), bool (*isOn)())
{
    if (pad < 1 || pad > kPads || !s.map[pad - 1].label) return;
    s.map[pad - 1].toggle = toggle;
    s.map[pad - 1].isOn = isOn;
    paint_cells(s.homeCells);
    paint_cells(s.overlayCells);
}

void mode_overlay_open()
{
    if (!map_is_home() || !foreground()) return;
    open_over_app();
}

void mode_overlay_close()
{
    close_more();
    ui_back_remove(s.backToken);
    s.backToken = 0;
    if (s.group) {
        lv_indev_t* enc = getGuiPlatform().getNavigationEncoder();
        if (enc && lv_indev_get_group(enc) == s.group) lv_indev_restore_group(enc, s.priorGroup);
        /* Closed from a cell's CLICKED, the encoder's input read is still
         * using this group; it goes once that read is over. */
        lv_group_delete_deferred(s.group);
        s.group = nullptr;
        s.priorGroup = nullptr;
    }
    if (s.overlay) {
        /* Same reason, for the cell being clicked: hidden now, gone after. */
        lv_obj_add_flag(s.overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(s.overlay);
        s.overlay = nullptr;
        s.overlayCells.fill(nullptr);
    }
    if (!home_in_charge()) release_pads();
    update_open();
    transportbar_refresh();
}

void mode_overlay_toggle()
{
    /* With the icon grid or the list as home, MODE is the way home. */
    if (!map_is_home()) { mode_overlay_go_home(); return; }
    if (s.overlay)          mode_overlay_close();
    else if (foreground())  open_over_app();
    /* At home the map is the screen already. */
}

bool mode_overlay_is_open() { return s_open.load(); }

void mode_overlay_select_pad(uint8_t padIdx)
{
    if (padIdx >= kPads) return;
    getGuiPlatform().postToGui(async_select, reinterpret_cast<void*>(static_cast<uintptr_t>(padIdx)));
}

void mode_overlay_go_home()
{
    mode_overlay_close();
    /* Going home is also how a flow is left: whatever Arrange handed to the
     * Recorder or the sequencer is no longer being filled. */
    crosspad::arrangeFocus().clear();
    /* Everything on screen goes, the kit browser over an app included. What
     * holds the transport keeps playing: a claim outlives its screen. Bounded,
     * because an app whose teardown fails would otherwise stay foreground. */
    for (int i = 0; i < 4; ++i) {
        ILvglApp* app = foreground();
        if (!app) break;
        app->destroyApp();
    }
    schedule_check();
}

bool mode_overlay_open_app(const char* name)
{
    if (!name || !s.appContainer) return false;
    auto app = find_app(name);
    if (!app) return false;
    if (AppOrchestrator::getInstance().getRunningApp() == app.get() && app->isStarted()) return false;
    AppOrchestrator::onAppSelected(app.get(), s.appContainer);
    transportbar_refresh();
    return true;
}

} // namespace crosspad_gui

#endif // USE_LVGL
