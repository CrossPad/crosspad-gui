#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Get contrast (black/white) text color for a given background color
lv_color_t lv_color_bw_from_color(lv_color_t color);

// Set object visibility (wraps LV_OBJ_FLAG_HIDDEN)
void lv_obj_set_hidden(lv_obj_t * obj, bool hidden);

// Delete all children from an LVGL list/container
void lv_list_cleanup(lv_obj_t * list);

/* A group pointer saved to be restored later can be dead by the time the
 * restore runs: an app's teardown deletes its groups synchronously, while the
 * nested overlays that saved them are only reached by the *async* deletion of
 * the app's tree afterwards. lv_group_delete() detaches the indev from a group
 * it is deleting, but nothing stops a later write from putting the freed
 * pointer back — and the next encoder read then dereferences it
 * (lv_group_get_focused, LoadProhibited).
 *
 * lv_indev_restore_group() is that write, made safe: a group LVGL no longer
 * knows about is not restored, and whatever the framework left on the indev
 * stands. */
bool lv_group_is_alive(const lv_group_t * group);
void lv_indev_restore_group(lv_indev_t * indev, lv_group_t * group);

/* Delete a group once the input read that is running has finished with it.
 * A window closed from its own button's CLICKED cannot delete its group
 * there: when the click came from the encoder, LVGL sends LV_KEY_ENTER to
 * that group right after CLICKED returns (indev_encoder_proc), and a group
 * deleted in the handler is freed memory by then -- a LoadProhibited in
 * lv_obj_get_state on the LVGL task. Point the indev elsewhere first. */
void lv_group_delete_deferred(lv_group_t * group);

// Calculate number of digits in an integer (for spinbox sizing)
int CalcDigits(int value);

// A button the encoder can edit, like a slider: a click on it enters edit mode,
// where turning the encoder sends it LV_EVENT_KEY with LV_KEY_LEFT/RIGHT, and
// the next click sends LV_EVENT_CLICKED (the owner leaves edit mode there with
// lv_group_set_editing(group, false)). Touch sees a plain button. For a value
// that is shown as text and turned, where a spinbox's text area scrolls its
// own digits out of sight.
lv_obj_t * lv_CreateEditableButton(lv_obj_t * parent);

#endif // USE_LVGL
