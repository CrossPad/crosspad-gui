// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_test_platform.h"

#include "lvgl_headless.h"

#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad/platform/CrosspadPlatformInit.hpp"

namespace guitest {

namespace {
void encoder_read(lv_indev_t*, lv_indev_data_t* data)
{
    data->state = LV_INDEV_STATE_RELEASED;
    data->enc_diff = 0;
}
} // namespace

GuiFixture::GuiFixture()
{
    lvgl_headless_init();

    gui.encoder = lv_indev_create();
    lv_indev_set_type(gui.encoder, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(gui.encoder, encoder_read);
    crosspad_gui::setGuiPlatform(&gui);

    crosspad::CrosspadPlatformConfig config;
    config.eventBus         = &eventBus;
    config.clock            = &clock;
    config.ledStrip         = &ledStrip;
    config.midiOutput       = &midiOutput;
    config.padManager       = &padManager;
    config.padLedController = &padLedController;
    config.padAnimator      = &padAnimator;
    config.status           = &status;
    crosspad::crosspad_platform_init(config);

    screen = lv_obj_create(nullptr);
    lv_screen_load(screen);
    group = lv_group_create();
    lv_indev_set_group(gui.encoder, group);
}

GuiFixture::~GuiFixture()
{
    lv_timer_handler();   // async calls a test queued still point at its objects
    lv_obj_clean(lv_layer_top());
    lv_obj_t* blank = lv_obj_create(nullptr);
    lv_screen_load(blank);
    lv_obj_delete(screen);
    lv_indev_delete(gui.encoder);
    lv_group_delete(group);
    lv_timer_handler();
    crosspad_gui::setGuiPlatform(nullptr);
    crosspad::crosspad_platform_reset();
}

void GuiFixture::advance(uint32_t ms)
{
    lv_tick_inc(ms);
    lv_timer_handler();
}

} // namespace guitest
