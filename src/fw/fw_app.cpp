// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef USE_LVGL

#include "crosspad-gui/fw/fw_app.h"

#include <memory>

#include "crosspad-gui/fw/IFwViewService.h"
#include "crosspad-gui/fw/fw_actions.h"
#include "crosspad-gui/fw/fw_kit_card.h"
#include "crosspad-gui/fw/fw_library_view.h"
#include "crosspad-gui/fw/fw_store_view.h"
#include "crosspad-gui/fw/fw_updates_view.h"
#include "crosspad-gui/fw/fw_views_common.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad/pad/IPadLogicHandler.hpp"
#include "crosspad/pad/PadLedController.hpp"
#include "crosspad/pad/PadManager.hpp"

namespace crosspad_gui {

using crosspad::IPadLogicHandler;
using crosspad::PadManager;
using crosspad::RgbColor;

static constexpr int32_t kTabBarH = 26;
static constexpr uint32_t kTickMs = 250;
static constexpr size_t kTailMax = 4;

static App * s_app = nullptr;
static const FwAppHooks * s_hooks = nullptr;
static lv_group_t * s_group = nullptr;
static lv_obj_t * s_tabs = nullptr;
static lv_timer_t * s_tick = nullptr;
static int s_shownTab = -1;

static lv_group_t * home_group() { return s_hooks && s_hooks->homeGroup ? s_hooks->homeGroup() : nullptr; }

// ── Tabs ────────────────────────────────────────────────────────────────

static lv_obj_t * s_updatesBox = nullptr;

static void updates_tab_build(lv_obj_t * page, lv_group_t * group) {
    s_updatesBox = lv_obj_create(page);
    lv_obj_remove_style_all(s_updatesBox);
    lv_obj_set_size(s_updatesBox, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_updatesBox, LV_FLEX_FLOW_COLUMN);
    fw_updates_view_build(s_updatesBox, group);
    lv_obj_t * tail[kTailMax];
    const size_t n = s_hooks->buildUpdatesTail ? s_hooks->buildUpdatesTail(page, group, tail, kTailMax) : 0;
    fw_updates_view_set_tail(tail, n);
    fw_updates_view_shown();
}

static void updates_tab_close(void) {
    fw_updates_view_close();
    if (s_updatesBox) {
        lv_obj_add_flag(s_updatesBox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_delete_async(s_updatesBox);
    }
    s_updatesBox = nullptr;
    if (s_hooks->closeUpdatesTail) s_hooks->closeUpdatesTail(true);
}

static void store_tab_build(lv_obj_t * page, lv_group_t * group) {
    fw_store_view_build(page, group);
    fw_store_view_shown();
}

struct AppTab {
    const char * title;
    void (*build)(lv_obj_t * page, lv_group_t * group);
    void (*refresh)(void);
    void (*close)(void);
    bool scrolls;   // a list page scrolls; a grid page scrolls its grid instead
};

static const AppTab kAppTabs[] = {
    {"Library", fw_library_view_build, fw_library_view_refresh, fw_library_view_close, false},
    {"Store", store_tab_build, fw_store_view_refresh, fw_store_view_close, false},
    {"Updates", updates_tab_build, fw_updates_view_refresh, updates_tab_close, true},
};
static constexpr int kAppTabCount = static_cast<int>(sizeof(kAppTabs) / sizeof(kAppTabs[0]));

static lv_obj_t * page_of(int i) {
    return lv_obj_get_child(lv_tabview_get_content(s_tabs), i);
}

/* One page at a time: the one shown is built, the one left is deleted. */
static void show_tab(int i) {
    if (i == s_shownTab || i < 0 || i >= kAppTabCount) return;
    if (s_shownTab >= 0) kAppTabs[s_shownTab].close();
    s_shownTab = i;
    kAppTabs[i].build(page_of(i), s_group);
    fw_views_focus_first(page_of(i), s_group);
}

/* A tab button's CLICKED, not the tabview's VALUE_CHANGED: LVGL 9.3 sends that
 * only when a swipe ends on another page, and a press makes the tab active
 * before its scroll, so the scroll's end finds nothing changed. The tabview's
 * own handler was added with the button and has run by now. */
static void tab_button_clicked_cb(lv_event_t *) {
    if (s_tabs) show_tab(static_cast<int>(lv_tabview_get_tab_active(s_tabs)));
}

static void tick_cb(lv_timer_t *) {
    if (s_shownTab >= 0) kAppTabs[s_shownTab].refresh();
}

// ── Pad Logic Handler ───────────────────────────────────────────────────

class FirmwarePadHandler : public IPadLogicHandler {
public:
    void onActivate(PadManager & pm) override {
        for (uint8_t i = 0; i < 16; i++) {
            pm.setPadColor(i, RgbColor(0, 8, 15));
        }
    }

    void onDeactivate(PadManager & pm) override {
        for (uint8_t i = 0; i < 16; i++) {
            pm.setPadColor(i, pm.getPadColor(i));
        }
    }

    void onPadPress(PadManager & pm, uint8_t padIdx, uint8_t velocity) override {
        pm.setPadColor(padIdx, RgbColor(30, 30, 30));
        crosspad::getPadLedController().notifyNoteOn(padIdx, velocity);
    }

    void onPadRelease(PadManager & pm, uint8_t padIdx) override {
        pm.setPadColor(padIdx, RgbColor(0, 8, 15));
        crosspad::getPadLedController().notifyNoteOff(padIdx);
    }

    void onPadPressure(PadManager & pm, uint8_t padIdx, uint8_t pressure) override {
        uint8_t b = 8 + (pressure / 10);
        pm.setPadColor(padIdx, RgbColor(0, b / 2, b));
    }
};

static std::shared_ptr<FirmwarePadHandler> s_padHandler;

// ── App factory / lifecycle ─────────────────────────────────────────────

lv_obj_t * fw_app_create(lv_obj_t * parent, App * a, const FwAppHooks & hooks) {
    s_app = a;
    s_hooks = &hooks;

    lv_obj_t * container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    s_group = lv_group_create();
    s_tabs = lv_tabview_create(container);
    lv_tabview_set_tab_bar_size(s_tabs, kTabBarH);
    lv_obj_set_size(s_tabs, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_tabs, lv_color_black(), 0);
    // Tabs change by the tab bar only: a swipe would show a page that is not built yet.
    lv_obj_remove_flag(lv_tabview_get_content(s_tabs), LV_OBJ_FLAG_SCROLLABLE);
    for (const AppTab & t : kAppTabs) {
        lv_obj_t * page = lv_tabview_add_tab(s_tabs, t.title);
        lv_obj_set_style_pad_all(page, kFwPad, 0);
        lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(page, kFwPad, 0);
        if (!t.scrolls) lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_t * bar = lv_tabview_get_tab_bar(s_tabs);
    for (uint32_t i = 0; i < lv_obj_get_child_count(bar); ++i) {
        lv_obj_t * button = lv_obj_get_child(bar, static_cast<int32_t>(i));
        lv_group_add_obj(s_group, button);
        lv_obj_add_event_cb(button, tab_button_clicked_cb, LV_EVENT_CLICKED, nullptr);
    }
    lv_indev_t * nav = getGuiPlatform().getNavigationEncoder();
    if (nav) lv_indev_set_group(nav, s_group);

    fwViewService().uiHost("app");
    show_tab(0);
    s_tick = lv_timer_create(tick_cb, kTickMs, nullptr);

    if (!s_padHandler) {
        s_padHandler = std::make_shared<FirmwarePadHandler>();
        crosspad::getPadManager().registerPadLogic("Firmware", s_padHandler);
    }

    a->setOnShow([](lv_obj_t *) {
        lv_indev_t * enc = getGuiPlatform().getNavigationEncoder();
        if (s_group && enc) lv_indev_set_group(enc, s_group);
        fwViewService().uiHost("app");
        if (s_hooks && s_hooks->shown) s_hooks->shown();
    });

    a->setOnHide([](lv_obj_t *) {
        lv_indev_restore_group(getGuiPlatform().getNavigationEncoder(), home_group());
    });

    return container;
}

void fw_app_destroy(lv_obj_t * app) {
    if (s_tick) {
        lv_timer_delete(s_tick);
        s_tick = nullptr;
    }
    // A flow overlay in progress is unaffected -- it owns its widgets and tasks.
    fw_actions_forget();   // first: an open confirm hands the encoder back to the card it came from
    fw_kit_card_close();   // its encoder group sits on top of ours
    if (app && s_shownTab >= 0) {
        kAppTabs[s_shownTab].close();
    } else {
        // The root is being deleted with the screen: every view forgets
        // itself as its own root goes, and nothing here may delete into it.
        s_updatesBox = nullptr;
        if (s_hooks && s_hooks->closeUpdatesTail) s_hooks->closeUpdatesTail(false);
    }
    s_shownTab = -1;
    s_tabs = nullptr;

    if (s_group) {
        lv_group_delete(s_group);
        s_group = nullptr;
    }
    lv_indev_restore_group(getGuiPlatform().getNavigationEncoder(), home_group());
    fwViewService().uiReset();
    s_app = nullptr;

    if (app) lv_obj_delete_async(app);
}

lv_obj_t * fw_app_container() { return s_app ? s_app->app_c : nullptr; }

} // namespace crosspad_gui

#endif // USE_LVGL
