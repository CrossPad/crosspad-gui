#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Callback types
typedef void (*modal_confirm_cb_t)(bool confirmed, void * user_data);
typedef void (*modal_select_cb_t)(int selected_index, bool cancelled, void * user_data);

// Confirmation dialog on lv_layer_top() with semi-transparent overlay.
// yes_text / no_text: button labels (e.g. "Yes"/"No", "Reset"/"Cancel")
// callback: called with true (yes) or false (no/close)
// encoder_group: if non-NULL, buttons are added to this group for encoder nav
lv_obj_t * ui_modal_confirm(
    const char * title,
    const char * message,
    const char * yes_text,
    const char * no_text,
    modal_confirm_cb_t callback,
    void * user_data,
    lv_group_t * encoder_group
);

// Close a confirm dialog from code (a timeout, a request withdrawn) exactly
// as a button press would: callback, encoder group restored, overlay deleted.
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
