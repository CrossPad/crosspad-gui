#ifdef USE_LVGL

#include "crosspad-gui/widgets/gui_helpers.h"

#include "src/core/lv_obj_class_private.h"   // lv_obj_class_t fields: a class of our own

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

lv_obj_t * lv_CreateEditableButton(lv_obj_t * parent) {
    /* lv_button_class exactly, except editable: same constructor and event
     * handler, so it looks and behaves like any button until an encoder
     * clicks it. */
    static lv_obj_class_t cls;
    if (cls.instance_size == 0) {
        cls = lv_button_class;
        cls.base_class = lv_button_class.base_class;
        cls.editable = LV_OBJ_CLASS_EDITABLE_TRUE;
        cls.name = "editable_button";
    }
    lv_obj_t * obj = lv_obj_class_create_obj(&cls, parent);
    lv_obj_class_init_obj(obj);
    /* The theme styles a button by its class, and this one is not
     * lv_button_class: without these it has no background at all. */
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    return obj;
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
