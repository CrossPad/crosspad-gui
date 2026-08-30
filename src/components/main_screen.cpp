#ifdef USE_LVGL

#include "crosspad-gui/components/main_screen.h"
#include "crosspad-gui/components/status_bar.h"
#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"

namespace crosspad_gui {

/* The previous build's launcher group. lv_obj_clean() takes the widgets but
 * not the group, so every return to the launcher leaked one lv_group_t —
 * ~70 B of internal RAM per app visit, visible as a slope in app_churn. */
static lv_group_t* s_encoder_group = nullptr;

MainScreenResult build_main_screen(
    lv_obj_t* parent,
    const LauncherAppInfo* apps,
    size_t app_count,
    const MainScreenConfig& config)
{
    MainScreenResult result;

    lv_obj_clean(parent);
    lv_obj_set_size(parent, CP_LCD_HOR_RES, CP_LCD_VER_RES);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, 0);
    lv_obj_set_style_pad_column(parent, 0, 0);
    lv_obj_set_style_bg_color(parent, config.bg_color, 0);

    result.encoder_group = lv_group_create();
    if (config.encoder) {
        lv_indev_set_group(config.encoder, result.encoder_group);
    }
    /* Only once the encoder points at the new group; nothing else holds the
     * old one by then. */
    if (s_encoder_group) lv_group_delete(s_encoder_group);
    s_encoder_group = result.encoder_group;

    result.status_bar = lv_CreateStatusBar(parent);

    lv_obj_t* app_c = lv_obj_create(parent);
    lv_obj_add_style(app_c, &styleAppContainer, 0);
    lv_obj_set_flex_grow(app_c, 1);
    lv_obj_update_layout(parent);
    lv_obj_set_width(app_c, lv_obj_get_content_width(parent));
    lv_obj_set_style_bg_color(app_c, config.bg_color, 0);
    lv_obj_set_style_bg_opa(app_c, LV_OPA_COVER, 0);
    lv_obj_remove_flag(app_c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(app_c, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_clip_corner(app_c, true, 0);
    lv_obj_update_layout(app_c);
    result.app_container = app_c;

    LauncherConfig lc = {};
    lc.encoder_group  = result.encoder_group;
    lc.on_select      = config.on_select;
    lc.on_close       = config.on_close;
    lc.button_size    = config.button_size;
    lc.button_spacing = config.button_spacing;
    lc.show_names     = config.show_names;

    result.launcher_grid = launcher_create_button_grid(
        app_c, apps, app_count, &lc);

    return result;
}

} // namespace crosspad_gui

#endif // USE_LVGL
