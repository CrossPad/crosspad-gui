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
    lv_obj_t* overlay = nullptr;
    std::array<lv_obj_t*, kPads> cells{};
    bool padLogicPushed = false;
    bool registered = false;
    /// Open with nothing under it: the map is home, not a way out of an app.
    bool asHome = false;
    /// The map's own encoder group, and the one it took the encoder from.
    lv_group_t* group = nullptr;
    lv_group_t* priorGroup = nullptr;
    uint32_t backToken = 0;
    /// The "More" list over the map, and its own back-chain entry.
    lv_obj_t* more = nullptr;
    uint32_t moreBackToken = 0;
    bool checkPending = false;
};

State s;
/* Read from the pad and MIDI tasks (mode_overlay_is_open()), so not a member
 * of the LVGL-only state above. */
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

const ModeEntry* entry_for_pad(uint8_t pad)
{
    if (pad < 1 || pad > kPads) return nullptr;
    const ModeEntry& e = s.map[pad - 1];
    return e.label ? &e : nullptr;
}

std::shared_ptr<ILvglApp> find_app(const char* name)
{
    if (!name) return nullptr;
    for (const auto& app : AppOrchestrator::getInstance().getApps()) {
        if (app && same_name(app->getName(), name)) return app;
    }
    return nullptr;
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

// ── Acting on a choice ─────────────────────────────────────────────────────

void paint_cells();
void open_map(bool asHome);
void schedule_check();
void open_more();
void close_more();

void choose(uint8_t pad)
{
    const ModeEntry* e = entry_for_pad(pad);
    if (!e) return;

    if (e->kind == ModeKind::Toggle) {
        if (e->toggle) e->toggle();
        mode_overlay_close();
        return;
    }

    if (e->kind == ModeKind::More) {
        open_more();
        return;
    }

    /* Hand the pads back before the next app takes them: its start() claims
     * its own pad logic, and ours must not be left on the stack under it. */
    mode_overlay_close();
    if (is_running(*e)) return;
    /* A pad chosen here is a player leaving whatever flow they were in, so
     * the Arrange track one app was filling is no longer being filled. Only
     * on this path: mode_overlay_open_app() is also how Arrange hands over,
     * and that hand-over is exactly what sets the focus. */
    crosspad::arrangeFocus().clear();
    mode_overlay_open_app(e->app);
    /* Chosen from home, the screen is empty until the app is up -- and stays
     * empty if it never comes up. */
    schedule_check();
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

/* Deleted asynchronously by mode_overlay_close(), which has already let go of
 * it; this matters only when someone else wipes the screen under the map. */
void on_overlay_delete(lv_event_t* ev)
{
    if (lv_event_get_target(ev) != s.overlay) return;
    s.overlay = nullptr;
    s.cells.fill(nullptr);
    s.more = nullptr;          // the list dies with the overlay
    s.moreBackToken = 0;
    s_open.store(false);
}

/// Power click with the map up: back to the app under it. At home there is
/// nothing under it, and the click goes on down the chain to nothing.
bool on_back(void*)
{
    if (!foreground()) return false;
    mode_overlay_close();
    return true;
}

/// What is on screen after an app started or stopped: the map comes up as
/// home when nothing is left, and a home map gives way to an app that came
/// up some other way (the kit browser handing back the app it was opened for).
void check_cb(void*)
{
    s.checkPending = false;
    if (!s.appContainer) return;
    if (!foreground()) {
        if (!s.overlay) open_map(true);
        else            s.asHome = true;
    } else if (s.overlay && s.asHome) {
        mode_overlay_close();
    }
}

/// Every app start goes through the orchestrator, and this runs first there:
/// the map's pad logic must be off the stack before the app under the map
/// pops its own and the next one pushes its own.
void close_before_launch()
{
    if (s.overlay || s.padLogicPushed) mode_overlay_close();
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

void build_cell(lv_obj_t* parent, const ModeEntry& e, int32_t x, int32_t y, int32_t w, int32_t h)
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

    s.cells[e.pad - 1] = c;
}

void paint_cells()
{
    for (int i = 0; i < kPads; ++i) {
        lv_obj_t* c = s.cells[i];
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

void build_overlay()
{
    lv_obj_t* parent = getOverlayParent();
    lv_obj_update_layout(parent);
    const int32_t W = lv_obj_get_width(parent);
    const int32_t H = lv_obj_get_height(parent) - TRANSPORT_BAR_HEIGHT;

    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, 0, TRANSPORT_BAR_HEIGHT);
    lv_obj_set_size(o, W, H);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, kBg, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);   // swallow taps between cells
    lv_obj_add_event_cb(o, on_overlay_delete, LV_EVENT_DELETE, nullptr);
    s.overlay = o;

    lv_obj_t* head = label(o, &lv_font_montserrat_10, kDim, "MODE  -  hit a pad or tap");
    lv_obj_set_pos(head, 8, 4);

    constexpr int32_t kMargin = 4, kGap = 4, kHead = 18;
    const int32_t cw = (W - 2 * kMargin - 3 * kGap) / 4;
    const int32_t ch = (H - kHead - kMargin - 3 * kGap) / 4;

    /* Row 0 on screen is pads 13-16: the screen is a picture of the pads. */
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const uint8_t pad = static_cast<uint8_t>((3 - row) * 4 + col + 1);
            const ModeEntry* e = entry_for_pad(pad);
            if (!e) continue;
            build_cell(o, *e, kMargin + col * (cw + kGap), kHead + row * (ch + kGap), cw, ch);
        }
    }
    paint_cells();
}

/// A row of the More list chosen: the same path a pad of the map takes.
void pick_more(const char* name)
{
    close_more();
    mode_overlay_close();
    crosspad::arrangeFocus().clear();
    mode_overlay_open_app(name);
    schedule_check();
}

/// Power click in the list: back to the map, not out of it.
bool on_more_back(void*)
{
    if (!s.more) return false;
    close_more();
    return true;
}

void open_more()
{
    if (!s.overlay || s.more) return;
    std::vector<const char*> registered;
    for (const auto& app : AppOrchestrator::getInstance().getApps()) {
        if (app) registered.push_back(app->getName());
    }
    std::vector<ModeEntry> map;
    for (const ModeEntry& e : s.map) if (e.label) map.push_back(e);
    const auto names = mode_more_apps(map.data(), map.size(), registered);

    s.more = lv_obj_create(s.overlay);
    lv_obj_remove_style_all(s.more);
    lv_obj_set_size(s.more, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s.more, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s.more, kBg, 0);
    lv_obj_add_flag(s.more, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* head = label(s.more, &lv_font_montserrat_10, kDim, "MORE  -  every other app");
    lv_obj_set_pos(head, 8, 4);
    lv_obj_t* body = lv_obj_create(s.more);
    lv_obj_remove_style_all(body);
    lv_obj_update_layout(s.overlay);
    lv_obj_set_pos(body, 0, 18);
    lv_obj_set_size(body, LV_PCT(100), lv_obj_get_height(s.overlay) - 18);

    if (s.group) lv_group_remove_all_objs(s.group);
    mode_more_build(body, names, pick_more, s.group);
    s.moreBackToken = ui_back_push(on_more_back, nullptr);
}

void close_more()
{
    if (!s.more) return;
    ui_back_remove(s.moreBackToken);
    s.moreBackToken = 0;
    lv_obj_add_flag(s.more, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(s.more);
    s.more = nullptr;
    if (s.group) {
        lv_group_remove_all_objs(s.group);
        for (lv_obj_t* c : s.cells) if (c) lv_group_add_obj(s.group, c);
        if (lv_obj_t* c8 = s.cells[7]) lv_group_focus_obj(c8);
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

void mode_overlay_init(lv_obj_t* app_container, const ModeEntry* entries, size_t count)
{
    s.appContainer = app_container;
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

void mode_overlay_bind_toggle(uint8_t pad, void (*toggle)(), bool (*isOn)())
{
    if (pad < 1 || pad > kPads || !s.map[pad - 1].label) return;
    s.map[pad - 1].toggle = toggle;
    s.map[pad - 1].isOn = isOn;
    if (s.overlay) paint_cells();
}

namespace {

void open_map(bool asHome)
{
    if (s.overlay) return;
    build_overlay();
    s.asHome = asHome;
    s_open.store(true);
    if (s.registered && !s.padLogicPushed) {
        crosspad::getPadManager().pushPadLogic(kPadLogicName);
        s.padLogicPushed = true;
    }

    /* The encoder walks the cells in pad order and a click chooses; the app
     * under the map must not be driven blind. */
    lv_indev_t* enc = getGuiPlatform().getNavigationEncoder();
    if (enc) {
        s.group = lv_group_create();
        lv_group_set_wrap(s.group, true);
        lv_obj_t* first = nullptr;
        for (int i = 0; i < kPads; ++i) {
            lv_obj_t* c = s.cells[i];
            if (!c) continue;
            lv_group_add_obj(s.group, c);
            const ModeEntry* e = entry_for_pad(i + 1);
            if (!first || (e && is_running(*e))) first = c;
        }
        if (first) lv_group_focus_obj(first);
        s.priorGroup = lv_indev_get_group(enc);
        lv_indev_set_group(enc, s.group);
    }
    s.backToken = ui_back_push(on_back, nullptr);
    transportbar_refresh();
}

} // namespace

void mode_overlay_open()
{
    open_map(false);
}

void mode_overlay_close()
{
    close_more();
    if (s.padLogicPushed) {
        crosspad::getPadManager().popPadLogic();
        s.padLogicPushed = false;
    }
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
        s.cells.fill(nullptr);
    }
    s.asHome = false;
    s_open.store(false);
    transportbar_refresh();
}

void mode_overlay_toggle()
{
    if (!s.overlay)    mode_overlay_open();
    else if (foreground()) mode_overlay_close();
    /* Home: nothing under the map to go back to, so MODE leaves it up. */
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
    if (s.appContainer) open_map(true);
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
