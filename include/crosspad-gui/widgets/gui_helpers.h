// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

/// Black or white, whichever reads better as text on @p color.
lv_color_t lv_color_bw_from_color(lv_color_t color);

/// Show or hide @p obj (sets or clears LV_OBJ_FLAG_HIDDEN). Safe with NULL.
void lv_obj_set_hidden(lv_obj_t * obj, bool hidden);

/// Delete all children of an LVGL list or container. Safe with NULL.
void lv_list_cleanup(lv_obj_t * list);

/// True while LVGL still knows @p group (it has not been deleted).
///
/// A group pointer saved to be restored later can be gone by the time the
/// restore runs: an app's teardown deletes its groups synchronously, while
/// the nested overlays that saved them are only reached by the asynchronous
/// deletion of the app's tree afterwards.
bool lv_group_is_alive(const lv_group_t * group);
/// Put @p group back on @p indev -- only if LVGL still knows the group;
/// otherwise whatever the framework left on the indev stands. Use it for
/// every restore of a saved encoder group.
void lv_indev_restore_group(lv_indev_t * indev, lv_group_t * group);

/// Delete a group once the input read that is running has finished with it.
/// Use it to close a window from its own button's CLICKED: when the click
/// came from the encoder, LVGL sends LV_KEY_ENTER to that group right after
/// CLICKED returns, so the group must outlive the handler. Point the indev
/// elsewhere first.
void lv_group_delete_deferred(lv_group_t * group);

/// Number of characters @p value takes in decimal, the minus sign included
/// (for spinbox sizing).
int CalcDigits(int value);

/// A button the encoder can edit, like a slider: a click on it enters edit mode,
/// where turning the encoder sends it LV_EVENT_KEY with LV_KEY_LEFT/RIGHT, and
/// the next click sends LV_EVENT_CLICKED (the owner leaves edit mode there with
/// lv_group_set_editing(group, false)). Touch sees a plain button. For a value
/// that is shown as text and turned, where a spinbox's text area scrolls its
/// own digits out of sight.
lv_obj_t * lv_CreateEditableButton(lv_obj_t * parent);

#endif // USE_LVGL
