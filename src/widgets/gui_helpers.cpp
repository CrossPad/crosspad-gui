#ifdef USE_LVGL

#include "crosspad-gui/widgets/gui_helpers.h"

lv_color_t lv_color_bw_from_color(lv_color_t color) {
    return ((color.red + color.green + color.blue) / 3) > 0x7F ? lv_color_black() : lv_color_white();
}

void lv_obj_set_hidden(lv_obj_t * obj, bool hidden) {
    if (obj == NULL) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void lv_list_cleanup(lv_obj_t * list) {
    if (list == NULL) return;
    while (lv_obj_get_child_count(list)) {
        lv_obj_t * child = lv_obj_get_child(list, 0);
        lv_obj_del(child);
    }
    LV_LOG_INFO("List cleaned up");
}

bool lv_group_is_alive(const lv_group_t * group) {
    if (group == NULL) return false;
    const uint32_t count = lv_group_get_count();
    for (uint32_t i = 0; i < count; i++) {
        if (lv_group_by_index(i) == group) return true;
    }
    return false;
}

void lv_indev_restore_group(lv_indev_t * indev, lv_group_t * group) {
    if (indev == NULL) return;
    if (group != NULL && !lv_group_is_alive(group)) {
        LV_LOG_WARN("Saved encoder group is gone; leaving the current one alone");
        return;
    }
    lv_indev_set_group(indev, group);
}

int CalcDigits(int value) {
    if (value == 0) return 1;
    int digits = 0;
    if (value < 0) {
        digits = 1;
        value = -value;
    }
    while (value > 0) {
        digits++;
        value /= 10;
    }
    return digits;
}

#endif // USE_LVGL
