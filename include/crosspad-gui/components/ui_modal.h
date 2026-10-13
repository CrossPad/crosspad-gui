// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Callback types
typedef void (*modal_confirm_cb_t)(bool confirmed, void * user_data);
typedef void (*modal_select_cb_t)(int selected_index, bool cancelled, void * user_data);
// The index of the button pressed, or -1 when the power button's back dismissed it.
typedef void (*modal_choice_cb_t)(int index, void * user_data);

// How a dialog button looks: the one to press (the accent), one that loses
// something (red), or any other.
typedef enum {
    UI_MODAL_BUTTON_NORMAL,
    UI_MODAL_BUTTON_PRIMARY,
    UI_MODAL_BUTTON_DANGER,
} ui_modal_button_kind_t;

typedef struct {
    const char *           text;
    ui_modal_button_kind_t kind;
} ui_modal_button_t;

// The look every dialog shares, for one that manages its own life (the
// Power OFF dialog): a dimmed overlay filling `parent`, a card in its middle
// with `title` and `message`, and an empty row for the buttons. Returns the
// overlay; `button_row` and `message_label` may be NULL.
lv_obj_t * ui_modal_frame(lv_obj_t * parent, const char * title, const char * message,
                          lv_obj_t ** button_row, lv_obj_t ** message_label);

// A button in a frame's row: an equal share of its width, a white ring when
// the encoder is on it.
lv_obj_t * ui_modal_button(lv_obj_t * button_row, const char * text, ui_modal_button_kind_t kind);

// A dialog of up to four buttons on lv_layer_top(); the encoder starts on
// `focus`. encoder_group: the group to give the encoder back; NULL = the one
// it had.
lv_obj_t * ui_modal_choice(
    const char * title,
    const char * message,
    const ui_modal_button_t * buttons,
    int count,
    int focus,
    modal_choice_cb_t callback,
    void * user_data,
    lv_group_t * encoder_group
);

// Confirmation dialog: ui_modal_choice() with yes (the accent) and no.
// yes_text / no_text: button labels (e.g. "Yes"/"No", "Reset"/"Cancel")
// callback: called with true (yes) or false (no/close)
// encoder_group: the group to give the encoder back; NULL = the one it had
lv_obj_t * ui_modal_confirm(
    const char * title,
    const char * message,
    const char * yes_text,
    const char * no_text,
    modal_confirm_cb_t callback,
    void * user_data,
    lv_group_t * encoder_group
);

// Close a confirm or choice dialog from code (a timeout, a request withdrawn,
// its screen going) exactly as a button press would: callback -- a choice
// gets -1 unless confirmed -- encoder group restored, overlay deleted.
// `overlay` is what ui_modal_confirm returned and must still be alive — the
// callback is the last moment the caller knows that.
void ui_modal_confirm_dismiss(lv_obj_t * overlay, bool confirmed);

// Selection list modal on lv_layer_top().
// options: NULL-terminated array of option strings
// current_selection: index of the currently selected option (highlighted)
// callback: called with selected index, or cancelled=true if dismissed
lv_obj_t * ui_modal_selection(
    const char * title,
    const char * options[],
    int current_selection,
    modal_select_cb_t callback,
    void * user_data,
    lv_group_t * encoder_group
);

#endif // USE_LVGL
