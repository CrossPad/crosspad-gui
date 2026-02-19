#ifdef USE_LVGL

#include "crosspad-gui/components/radial_menu.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RADIAL_MENU_RADIUS    80
#define RADIAL_MENU_ICON_SIZE 48
#define RADIAL_MENU_CENTER_X  160
#define RADIAL_MENU_CENTER_Y  120
#define RADIAL_MENU_MAX_ITEMS 8

static lv_obj_t * radial_container = nullptr;
static lv_obj_t * app_icons[RADIAL_MENU_MAX_ITEMS] = {nullptr};
static lv_obj_t * selector_arc = nullptr;
static lv_obj_t * center_label = nullptr;

static float current_angle = 0.0f;
static float target_angle = 0.0f;
static int selected_index = 0;
static lv_timer_t * anim_timer = nullptr;

// Store config for callbacks
static RadialMenuConfig s_config = {};

static float lerp(float start, float end, float t) {
    return start + (end - start) * t;
}

// Smooth rotation animation
static void radial_anim_cb(lv_timer_t * timer) {
    (void)timer;

    float diff = target_angle - current_angle;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;

    current_angle += diff * 0.15f;

    int total = s_config.item_count;
    if (total == 0) return;

    float angle_step = 360.0f / total;

    for (int i = 0; i < total && i < RADIAL_MENU_MAX_ITEMS; i++) {
        if (!app_icons[i]) continue;

        float icon_angle = current_angle + (i * angle_step);
        float rad = icon_angle * (float)M_PI / 180.0f;

        int x = RADIAL_MENU_CENTER_X + (int)(RADIAL_MENU_RADIUS * cosf(rad)) - RADIAL_MENU_ICON_SIZE / 2;
        int y = RADIAL_MENU_CENTER_Y + (int)(RADIAL_MENU_RADIUS * sinf(rad)) - RADIAL_MENU_ICON_SIZE / 2;

        lv_obj_set_pos(app_icons[i], x, y);

        int scale = (i == selected_index) ? 280 : 256;
        lv_obj_set_style_transform_scale(app_icons[i], scale, 0);

        if (i == selected_index) {
            lv_obj_set_style_border_color(app_icons[i], CROSSPAD_THEME_PRIMARY, 0);
            lv_obj_set_style_border_width(app_icons[i], 3, 0);
            lv_obj_set_style_shadow_width(app_icons[i], 20, 0);
            lv_obj_set_style_shadow_color(app_icons[i], CROSSPAD_THEME_PRIMARY, 0);
            lv_obj_set_style_shadow_opa(app_icons[i], LV_OPA_50, 0);
        } else {
            lv_obj_set_style_border_color(app_icons[i], lv_color_hex(0x333333), 0);
            lv_obj_set_style_border_width(app_icons[i], 2, 0);
            lv_obj_set_style_shadow_width(app_icons[i], 10, 0);
            lv_obj_set_style_shadow_color(app_icons[i], lv_color_black(), 0);
            lv_obj_set_style_shadow_opa(app_icons[i], LV_OPA_30, 0);
        }
    }

    if (selector_arc) {
        lv_arc_set_value(selector_arc, (int)current_angle % 360);
    }

    if (center_label && selected_index >= 0 && selected_index < total) {
        lv_label_set_text(center_label, s_config.items[selected_index].name);
    }
}

// Encoder key handler
static void radial_encoder_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        int total = s_config.item_count;
        if (total == 0) return;

        float angle_step = 360.0f / total;

        if (key == LV_KEY_LEFT) {
            selected_index = (selected_index + 1) % total;
            target_angle += angle_step;
        } else if (key == LV_KEY_RIGHT) {
            selected_index = (selected_index - 1 + total) % total;
            target_angle -= angle_step;
        } else if (key == LV_KEY_ENTER) {
            if (selected_index >= 0 && selected_index < total) {
                // Zoom animation before launching
                if (app_icons[selected_index]) {
                    for (int scale = 280; scale < 400; scale += 20) {
                        lv_obj_set_style_transform_scale(app_icons[selected_index], scale, 0);
                        lv_task_handler();
                        lv_tick_inc(16);
                    }
                }
                if (s_config.on_select) {
                    s_config.on_select(selected_index);
                }
            }
        }
    }
}

lv_obj_t * lv_CreateRadialMenu(lv_obj_t * parent, const RadialMenuConfig * config) {
    if (!config || config->item_count == 0) return nullptr;

    s_config = *config;
    selected_index = config->initial_index;
    current_angle = 0.0f;
    target_angle = 0.0f;

    // Main container
    radial_container = lv_obj_create(parent);
    lv_obj_set_size(radial_container, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(radial_container, CROSSPAD_THEME_BG_DARK, 0);
    lv_obj_set_style_border_width(radial_container, 0, 0);
    lv_obj_center(radial_container);

    // Selector arc
    selector_arc = lv_arc_create(radial_container);
    lv_obj_set_size(selector_arc, 100, 100);
    lv_obj_center(selector_arc);
    lv_arc_set_bg_angles(selector_arc, 0, 360);
    lv_arc_set_rotation(selector_arc, 270);
    lv_obj_set_style_arc_width(selector_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(selector_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(selector_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(selector_arc, CROSSPAD_THEME_PRIMARY, LV_PART_INDICATOR);
    lv_obj_remove_flag(selector_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(selector_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    // Center label
    center_label = lv_label_create(radial_container);
    lv_obj_set_style_text_color(center_label, CROSSPAD_THEME_PRIMARY, 0);
    lv_obj_set_style_text_font(center_label, &lv_font_montserrat_14, 0);
    lv_obj_align(center_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(center_label, "");

    // Create icons
    int total = config->item_count;
    float angle_step = 360.0f / (total > 0 ? total : 1);

    for (int i = 0; i < total && i < RADIAL_MENU_MAX_ITEMS; i++) {
        app_icons[i] = lv_obj_create(radial_container);
        lv_obj_set_size(app_icons[i], RADIAL_MENU_ICON_SIZE, RADIAL_MENU_ICON_SIZE);
        lv_obj_set_style_bg_color(app_icons[i], CROSSPAD_THEME_CARD_BG, 0);
        lv_obj_set_style_radius(app_icons[i], 8, 0);
        lv_obj_set_style_border_width(app_icons[i], 2, 0);
        lv_obj_set_style_border_color(app_icons[i], lv_color_hex(0x333333), 0);
        lv_obj_set_style_shadow_width(app_icons[i], 10, 0);
        lv_obj_set_style_shadow_color(app_icons[i], lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(app_icons[i], LV_OPA_30, 0);
        lv_obj_set_style_pad_all(app_icons[i], 6, 0);

        lv_obj_t * img = lv_image_create(app_icons[i]);
        lv_image_set_src(img, config->items[i].icon_src);
        lv_obj_center(img);

        // Initial position
        float icon_angle = current_angle + (i * angle_step);
        float rad = icon_angle * (float)M_PI / 180.0f;
        int x = RADIAL_MENU_CENTER_X + (int)(RADIAL_MENU_RADIUS * cosf(rad)) - RADIAL_MENU_ICON_SIZE / 2;
        int y = RADIAL_MENU_CENTER_Y + (int)(RADIAL_MENU_RADIUS * sinf(rad)) - RADIAL_MENU_ICON_SIZE / 2;
        lv_obj_set_pos(app_icons[i], x, y);
    }

    // Encoder input
    lv_obj_add_event_cb(radial_container, radial_encoder_cb, LV_EVENT_KEY, nullptr);
    if (config->input_group) {
        lv_group_add_obj(config->input_group, radial_container);
        lv_group_focus_obj(radial_container);
    }

    // Animation timer (60 FPS)
    anim_timer = lv_timer_create(radial_anim_cb, 16, nullptr);

    return radial_container;
}

void lv_DestroyRadialMenu() {
    if (anim_timer) {
        lv_timer_delete(anim_timer);
        anim_timer = nullptr;
    }

    if (radial_container) {
        lv_obj_delete(radial_container);
        radial_container = nullptr;
    }

    for (int i = 0; i < RADIAL_MENU_MAX_ITEMS; i++) {
        app_icons[i] = nullptr;
    }

    selector_arc = nullptr;
    center_label = nullptr;
    s_config = {};
}

#endif // USE_LVGL
