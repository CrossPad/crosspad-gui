// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/components/power_gesture.h"
#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/components/transport_bar.h"
#include "crosspad-gui/components/app_orchestrator.h"
#include "crosspad-gui/components/mode_overlay.h"
#include "crosspad-gui/platform/IGuiPlatform.h"

namespace crosspad_gui {

// ── Overlay back-handler stack ───────────────────────────────────────────────

namespace {

struct BackSlot {
    uint32_t        token = 0;
    BackHandlerFn   fn    = nullptr;
    void*           user  = nullptr;
};

constexpr int kMaxBackSlots = 8;
BackSlot  s_slots[kMaxBackSlots];
int       s_slotCount = 0;
uint32_t  s_nextToken = 1;

lv_group_t* navGroup()
{
    lv_indev_t* nav = getGuiPlatform().getNavigationEncoder();
    return nav ? lv_indev_get_group(nav) : nullptr;
}

/* An open dropdown is not necessarily the focused object of the navigation
 * group — one opened by touch never is, and LVGL puts its list on the top
 * layer rather than under the dropdown itself. So the tree is searched. It is
 * a handful of objects deep and this runs once per button press. */
lv_obj_t* findOpenDropdown(lv_obj_t* obj)
{
    if (!obj) return nullptr;
    if (lv_obj_check_type(obj, &lv_dropdown_class) && lv_dropdown_is_open(obj)) return obj;
    const uint32_t children = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < children; i++) {
        if (lv_obj_t* found = findOpenDropdown(lv_obj_get_child(obj, i))) return found;
    }
    return nullptr;
}

} // namespace

const char* backResultName(BackResult r)
{
    switch (r) {
        case BackResult::Dropdown: return "dropdown";
        case BackResult::Overlay:  return "overlay";
        case BackResult::Editing:  return "editing";
        case BackResult::Drawer:   return "drawer";
        case BackResult::Transport: return "transport";
        case BackResult::App:      return "app";
        default:                   return "none";
    }
}

uint32_t ui_back_push(BackHandlerFn fn, void* user_data)
{
    if (!fn || s_slotCount >= kMaxBackSlots) return 0;
    const uint32_t token = s_nextToken++;
    if (s_nextToken == 0) s_nextToken = 1;
    s_slots[s_slotCount++] = BackSlot{token, fn, user_data};
    return token;
}

void ui_back_remove(uint32_t token)
{
    if (token == 0) return;
    for (int i = 0; i < s_slotCount; i++) {
        if (s_slots[i].token != token) continue;
        for (int j = i; j < s_slotCount - 1; j++) s_slots[j] = s_slots[j + 1];
        s_slots[--s_slotCount] = BackSlot{};
        return;
    }
}

bool ui_back_has_overlay() { return s_slotCount > 0; }

// ── Emulated button timing ───────────────────────────────────────────────────

namespace {
lv_timer_t* s_holdTimer = nullptr;

/* An emulated button is never the one that woke the device and has no sleep
 * to come out of: it starts released, awake, with the GUI up. */
PowerGesture feedEmulated(bool pressed)
{
    static PowerButtonClassifier s_classifier = [] {
        PowerButtonClassifier c;
        c.feed(PowerButtonClassifier::Sample{false, true, false, true, lv_tick_get()});
        return c;
    }();
    return s_classifier.feed(PowerButtonClassifier::Sample{pressed, true, false, true, lv_tick_get()}).gesture;
}

void hold_timer_cb(lv_timer_t* t)
{
    (void)t;
    s_holdTimer = nullptr;   // repeat_count 1: LVGL deletes it after this call
    if (feedEmulated(true) == PowerGesture::Hold) powerToggleQuickSettings();
}
} // namespace

void powerButtonPress()
{
    if (s_holdTimer) { lv_timer_delete(s_holdTimer); s_holdTimer = nullptr; }
    feedEmulated(true);
    s_holdTimer = lv_timer_create(hold_timer_cb, kPowerHoldMs, nullptr);
    lv_timer_set_repeat_count(s_holdTimer, 1);
    /* Visual feedback for the same threshold the timer above is counting
     * down — a no-op when the drawer is already open, since there is
     * nothing to charge. Not with the transport on screen: there the hold
     * jumps the encoder rather than opening the drawer, and the arrow it
     * would charge is on the status bar's page, out of sight -- the charge's
     * end hides it, and it stayed hidden when that page came back. */
    if (!transportbar_shown()) statusbar_arrow_hold_begin(kPowerHoldMs);
}

void powerButtonRelease()
{
    if (s_holdTimer) { lv_timer_delete(s_holdTimer); s_holdTimer = nullptr; }
    if (feedEmulated(false) == PowerGesture::Click) {
        /* Released before the threshold: the charge animation (if any)
         * eases back rather than snapping, then the click is handled as
         * the ordinary contextual back. */
        statusbar_arrow_hold_cancel();
        powerBack();
    }
}

// ── Gestures ─────────────────────────────────────────────────────────────────

bool powerToggleQuickSettings()
{
    /* An open drawer closes first, whichever page of the top bar it hangs
     * from. With the transport on screen the hold jumps the encoder's focus
     * to its keys, and a second hold jumps it back -- the drawer has the
     * downward swipe from the top edge for a finger. With the status icons
     * on screen, or no transport bar at all, the hold is the drawer's, as
     * the arrow's charge on the press already said. */
    if (statusbar_drawer_is_open()) return statusbar_toggle_drawer();
    if (transportbar_toggle_focus()) return true;
    return statusbar_toggle_drawer();
}

BackResult powerBack()
{
    lv_group_t* group = navGroup();

    /* 1. An open dropdown list is the most local thing on screen — and it is
     *    LVGL's own object, not one of ours on the overlay stack. */
    lv_obj_t* dropdown = findOpenDropdown(lv_layer_top());
    if (!dropdown) dropdown = findOpenDropdown(lv_screen_active());
    if (dropdown) {
        lv_dropdown_close(dropdown);
        if (group) lv_group_set_editing(group, false);
        return BackResult::Dropdown;
    }

    /* 2. Modals / confirm boxes, newest first. A handler that declines is
     *    left alone — the next one down gets the gesture. */
    for (int i = s_slotCount - 1; i >= 0; i--) {
        BackSlot slot = s_slots[i];          // copy: the handler edits the stack
        if (slot.fn && slot.fn(slot.user)) return BackResult::Overlay;
    }

    /* 3. Encoder edit mode (a slider or spinbox being turned) — the same exit
     *    a click of the encoder itself performs. */
    if (group && lv_group_get_editing(group)) {
        lv_group_set_editing(group, false);
        return BackResult::Editing;
    }

    /* 4. Quick-settings drawer. */
    if (statusbar_drawer_is_open()) {
        statusbar_close_drawer();
        return BackResult::Drawer;
    }

    /* 4b. The encoder on the top bar's keys: back to the screen it left,
     *     rather than out of the app behind it -- one step back, not two.
     *     A focus the bar cannot hand back is no step: the app's turn. */
    if (transportbar_has_focus() && transportbar_toggle_focus()) {
        return BackResult::Transport;
    }

    /* 5. The running app: its own back hook first, then the app it was
     *    opened from (AppOrchestrator::historyBack()), and home -- the
     *    launcher under it -- once there is none.
     *    getForegroundApp(), not getRunningApp(): an app started from the
     *    pre-launch hook (the sampler's kit-selector gate) owns the screen
     *    without ever becoming runningApp_, and that screen has no other way
     *    out. */
    ILvglApp* app = AppOrchestrator::getInstance().getForegroundApp();
    if (app) {
        if (app->handleBack()) return BackResult::App;
        if (ILvglApp* prev = AppOrchestrator::getInstance().historyBack(app)) {
            /* Deferred like destroyAppAsync(): this can run inside an event
             * of the screen about to go. By name, through the MODE map's way
             * in, so the hand-over is the one every other switch makes. */
            lv_async_call([](void* p) {
                auto* to = static_cast<ILvglApp*>(p);
                if (ILvglApp* fg = AppOrchestrator::getInstance().getForegroundApp(); fg && fg != to) {
                    fg->destroyApp();
                }
                if (!mode_overlay_open_app(to->getName())) AppOrchestrator::getInstance().clearHistory();
            }, prev);
            return BackResult::App;
        }
        AppOrchestrator::getInstance().clearHistory();
        app->destroyAppAsync();
        return BackResult::App;
    }

    /* 6. Home: back stops here -- deliberately nothing. */
    return BackResult::None;
}

} // namespace crosspad_gui

#endif // USE_LVGL
