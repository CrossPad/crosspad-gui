#ifdef USE_LVGL

#include "crosspad-gui/widgets/keypad_buttons.h"
#include "crosspad-gui/styles/styles.h"
#include <crosspad/pad/PadManager.hpp>

static const int notes[] = {13, 14, 15, 16, 9, 10, 11, 12, 5, 6, 7, 8, 1, 2, 3, 4};

lv_obj_t * createButtons(lv_obj_t * parent, lv_event_cb_t callback, void * userData, bool labels)
{
    const int NUM_ROWS = 4;
    const int NUM_COLS = 4;
    const int BTN_SIZE = 25;
    const int PADDING = 4;
    int i = 0;

    lv_obj_t * frame = lv_obj_create(parent);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(frame, NUM_COLS * (BTN_SIZE + PADDING) + PADDING, NUM_ROWS * (BTN_SIZE + PADDING) + PADDING);
    lv_obj_add_style(frame, &styleAppContainer, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_0, 0);
    for (int row = 0; row < NUM_ROWS; row++) {
        for (int col = 0; col < NUM_COLS; col++) {
            lv_obj_t * button = lv_btn_create(frame);
            lv_obj_set_size(button, BTN_SIZE, BTN_SIZE);
            lv_obj_align(button, LV_ALIGN_TOP_LEFT, col * (BTN_SIZE + PADDING), row * (BTN_SIZE + PADDING));
            lv_obj_set_style_bg_color(button, lv_color_hex(0xF0F0F0), 0);
            lv_obj_set_style_radius(button, 3, 0);
            lv_obj_set_style_border_width(button, 0, LV_STATE_FOCUSED);
            lv_obj_set_style_border_color(button, lv_color_hex(0xFF0000), LV_STATE_FOCUSED);
            lv_obj_set_style_border_opa(button, LV_OPA_100, LV_STATE_FOCUSED);

            // This button shows pad notes[i]-1 (the label is the pad number), so
            // paint that pad's colour — not pad i, which is the row-mirrored one.
            crosspad::RgbColor padColor = crosspad::getPadManager().getPadColor(notes[i] - 1);
            lv_obj_set_style_bg_color(button, lv_color_make(padColor.R, padColor.G, padColor.B), 0);

            lv_obj_t * label_btn = lv_label_create(button);
            lv_label_set_text_fmt(label_btn, "%d", notes[i]);
            lv_obj_center(label_btn);
            if (!labels) lv_obj_add_flag(label_btn, LV_OBJ_FLAG_HIDDEN);
            if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, userData);
            i++;
        }
    }
    return frame;
}

lv_obj_t * createButtonsSequencer(lv_obj_t * parent, lv_event_cb_t callback, void * userData)
{
    const int NUM_ROWS = 4;
    const int NUM_COLS = 4;
    const int BTN_SIZE = 19;
    const int PADDING = 3;
    int i = 0;

    lv_obj_t * frame = lv_obj_create(parent);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(frame, NUM_COLS * (BTN_SIZE + PADDING) + PADDING, NUM_ROWS * (BTN_SIZE + PADDING) + PADDING);
    lv_obj_add_style(frame, &styleAppContainer, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_0, 0);
    for (int row = 0; row < NUM_ROWS; row++) {
        for (int col = 0; col < NUM_COLS; col++) {
            lv_obj_t * button = lv_btn_create(frame);
            lv_obj_set_size(button, BTN_SIZE, BTN_SIZE);
            lv_obj_align(button, LV_ALIGN_TOP_LEFT, col * (BTN_SIZE + PADDING), row * (BTN_SIZE + PADDING));
            lv_obj_set_style_bg_color(button, lv_color_hex(0x808080), 0);
            lv_obj_set_style_radius(button, 2, 0);
            lv_obj_set_style_border_width(button, 1, 0);
            lv_obj_set_style_border_color(button, lv_color_hex(0x606060), 0);
            lv_obj_set_style_border_width(button, 2, LV_STATE_FOCUSED);
            lv_obj_set_style_border_color(button, lv_color_hex(0xFF0000), LV_STATE_FOCUSED);
            lv_obj_set_style_border_opa(button, LV_OPA_100, LV_STATE_FOCUSED);

            crosspad::RgbColor padColor = crosspad::getPadManager().getPadColor(i);
            lv_obj_set_style_bg_color(button, lv_color_make(padColor.R, padColor.G, padColor.B), 0);

            if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, userData);
            i++;
        }
    }
    return frame;
}

#endif // USE_LVGL
