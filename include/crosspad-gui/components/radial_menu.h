#pragma once

#ifdef USE_LVGL
#include "lvgl.h"
#include <cstdint>

// Radial menu item descriptor
struct RadialMenuItem {
    const char * name;        // Display name
    const void * icon_src;    // LVGL image source (path string or lv_image_dsc_t *)
};

// Callback when an item is selected (confirmed with encoder press)
// @param index  Index of the selected item
typedef void (*radial_menu_select_cb_t)(int index);

// Configuration for the radial menu
struct RadialMenuConfig {
    RadialMenuItem * items;           // Array of menu items
    int item_count;                   // Number of items
    lv_group_t * input_group;         // Encoder group for navigation
    radial_menu_select_cb_t on_select; // Callback on item confirmation
    int initial_index;                // Initially selected item (default 0)
};

// Create a radial menu with icons arranged in a circle
lv_obj_t * lv_CreateRadialMenu(lv_obj_t * parent, const RadialMenuConfig * config);

// Destroy the radial menu and clean up timers
void lv_DestroyRadialMenu();

#endif // USE_LVGL
