#ifdef USE_LVGL

#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/widgets/gui_helpers.h"

/*      styles      */
lv_style_t styleStatusBar;
lv_style_t styleAppContainer;
lv_style_t styleAppPicker;
lv_style_t styleAppPickerPressed;
lv_style_t styleAppPickerInactive;
lv_style_t styleButton;
lv_style_t styleButtonPressed;
lv_style_t styleButtonInactive;
lv_style_t styleTitle;
lv_style_t styleBackButton;
lv_style_t stylePrimaryImgRecolor;
lv_style_t styleWaveformBox;
lv_style_t styleKitListBox;
lv_style_t styleKitListScroll;
lv_style_t styleKitListItem;
lv_style_t styleKitListItemFocused;
lv_style_t styleKitListItemSelected;
lv_style_t styleLoadingArc;
lv_style_t styleSpinbox;

// file explorer
lv_style_t styleFileExplorerList;
lv_style_t styleFileExplorerEntry;
lv_style_t styleFileExplorerEntrySelected;
lv_style_t styleFileExplorerScrollbar;

// settings
lv_style_t styleSettingsRow;
lv_style_t styleSettingsRowFocused;
lv_style_t styleSettingsHeader;
lv_style_t styleSettingsSection;

// Auto-computed text colors
#define TEXT_COLOR_ON_PRIMARY   lv_color_bw_from_color(CROSSPAD_THEME_PRIMARY)
#define TEXT_COLOR_ON_SECONDARY lv_color_bw_from_color(CROSSPAD_THEME_SECONDARY)

void initStyles() {
    // status bar style
    lv_style_init(&styleStatusBar);
    lv_style_set_bg_color(&styleStatusBar, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_color(&styleStatusBar, TEXT_COLOR_ON_PRIMARY);
    lv_style_set_text_font(&styleStatusBar, &lv_font_montserrat_14);
    lv_style_set_image_recolor(&styleStatusBar, TEXT_COLOR_ON_PRIMARY);
    lv_style_set_image_recolor_opa(&styleStatusBar, LV_OPA_100);
    lv_style_set_image_opa(&styleStatusBar, LV_OPA_100);
    lv_style_set_outline_width(&styleStatusBar, 0);
    lv_style_set_outline_color(&styleStatusBar, CROSSPAD_THEME_PRIMARY);
    lv_style_set_outline_pad(&styleStatusBar, 0);
    lv_style_set_pad_all(&styleStatusBar, 0);
    lv_style_set_border_width(&styleStatusBar, 0);
    lv_style_set_border_color(&styleStatusBar, lv_color_black());
    lv_style_set_clip_corner(&styleStatusBar, false);

    // app container style
    lv_style_init(&styleAppContainer);
    lv_style_set_bg_color(&styleAppContainer, CROSSPAD_THEME_BG_DARK);
    lv_style_set_border_width(&styleAppContainer, 0);
    lv_style_set_border_color(&styleAppContainer, lv_color_black());
    lv_style_set_pad_all(&styleAppContainer, 0);
    lv_style_set_clip_corner(&styleAppContainer, false);

    // app picker style
    lv_style_init(&styleAppPicker);
    lv_style_set_bg_color(&styleAppPicker, CROSSPAD_THEME_CARD_BG);
    lv_style_set_text_color(&styleAppPicker, lv_color_make(204, 204, 204));
    lv_style_set_text_font(&styleAppPicker, &lv_font_montserrat_24);
    lv_style_set_image_recolor(&styleAppPicker, lv_color_white());
    lv_style_set_image_recolor_opa(&styleAppPicker, LV_OPA_COVER);
    lv_style_set_image_opa(&styleAppPicker, LV_OPA_100);
    lv_style_set_border_width(&styleAppPicker, 2);
    lv_style_set_border_color(&styleAppPicker, lv_color_make(51, 51, 51));
    lv_style_set_radius(&styleAppPicker, 8);
    lv_style_set_shadow_width(&styleAppPicker, 10);
    lv_style_set_shadow_color(&styleAppPicker, lv_color_black());
    lv_style_set_shadow_opa(&styleAppPicker, LV_OPA_50);
    lv_style_set_shadow_spread(&styleAppPicker, 3);
    lv_style_set_pad_all(&styleAppPicker, 6);

    // app picker pressed/focused style
    lv_style_init(&styleAppPickerPressed);
    lv_style_set_bg_color(&styleAppPickerPressed, CROSSPAD_THEME_CARD_BG);
    lv_style_set_text_color(&styleAppPickerPressed, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_font(&styleAppPickerPressed, &lv_font_montserrat_24);
    lv_style_set_image_recolor(&styleAppPickerPressed, lv_color_white());
    lv_style_set_image_recolor_opa(&styleAppPickerPressed, LV_OPA_COVER);
    lv_style_set_image_opa(&styleAppPickerPressed, LV_OPA_100);
    lv_style_set_border_width(&styleAppPickerPressed, 2);
    lv_style_set_border_color(&styleAppPickerPressed, CROSSPAD_THEME_PRIMARY);
    lv_style_set_shadow_width(&styleAppPickerPressed, 20);
    lv_style_set_shadow_color(&styleAppPickerPressed, CROSSPAD_THEME_PRIMARY);
    lv_style_set_shadow_opa(&styleAppPickerPressed, LV_OPA_30);
    lv_style_set_shadow_spread(&styleAppPickerPressed, 6);
    lv_style_set_pad_all(&styleAppPickerPressed, 6);

    // app picker inactive style
    lv_style_init(&styleAppPickerInactive);

    // button style
    lv_style_init(&styleButton);
    lv_style_set_bg_color(&styleButton, CROSSPAD_THEME_SECONDARY);
    lv_style_set_text_color(&styleButton, TEXT_COLOR_ON_SECONDARY);
    lv_style_set_text_font(&styleButton, &lv_font_montserrat_14);
    lv_style_set_radius(&styleButton, 10);

    // button pressed style
    lv_style_init(&styleButtonPressed);
    lv_style_set_bg_color(&styleButtonPressed, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_color(&styleButtonPressed, TEXT_COLOR_ON_PRIMARY);
    lv_style_set_text_font(&styleButtonPressed, &lv_font_montserrat_14);

    // button inactive style
    lv_style_init(&styleButtonInactive);
    lv_style_set_bg_color(&styleButtonInactive, lv_color_darken(CROSSPAD_THEME_SECONDARY, 60));
    lv_style_set_bg_opa(&styleButtonInactive, LV_OPA_90);

    // title style
    lv_style_init(&styleTitle);
    lv_style_set_text_font(&styleTitle, &lv_font_montserrat_18);
    lv_style_set_text_color(&styleTitle, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_opa(&styleTitle, LV_OPA_100);

    // standard back button style
    lv_style_init(&styleBackButton);
    lv_style_set_bg_color(&styleBackButton, lv_color_white());
    lv_style_set_text_color(&styleBackButton, lv_color_black());
    lv_style_set_pad_all(&styleBackButton, 8);
    lv_style_set_radius(&styleBackButton, 6);
    lv_style_set_border_width(&styleBackButton, 1);
    lv_style_set_border_color(&styleBackButton, lv_color_make(51, 51, 51));

    // primary image recolor style
    lv_style_init(&stylePrimaryImgRecolor);
    lv_style_set_image_recolor(&stylePrimaryImgRecolor, CROSSPAD_THEME_PRIMARY);
    lv_style_set_image_recolor_opa(&stylePrimaryImgRecolor, LV_OPA_COVER);
    lv_style_set_image_opa(&stylePrimaryImgRecolor, LV_OPA_COVER);
    lv_style_set_bg_opa(&stylePrimaryImgRecolor, LV_OPA_TRANSP);
    lv_style_set_border_width(&stylePrimaryImgRecolor, 0);

    // waveform box style
    lv_style_init(&styleWaveformBox);
    lv_style_set_bg_color(&styleWaveformBox, lv_color_black());
    lv_style_set_bg_opa(&styleWaveformBox, LV_OPA_20);
    lv_style_set_border_width(&styleWaveformBox, 2);
    lv_style_set_border_color(&styleWaveformBox, CROSSPAD_THEME_SECONDARY);
    lv_style_set_radius(&styleWaveformBox, 7);
    lv_style_set_pad_all(&styleWaveformBox, 5);
    lv_style_set_outline_width(&styleWaveformBox, 1);
    lv_style_set_text_color(&styleWaveformBox, CROSSPAD_THEME_SECONDARY);
    lv_style_set_text_font(&styleWaveformBox, &lv_font_montserrat_14);
    lv_style_set_text_opa(&styleWaveformBox, LV_OPA_100);

    // kit list box style
    lv_style_init(&styleKitListBox);
    lv_style_set_bg_color(&styleKitListBox, lv_color_black());
    lv_style_set_pad_all(&styleKitListBox, 3);
    lv_style_set_radius(&styleKitListBox, 0);
    lv_style_set_outline_width(&styleKitListBox, 0);
    lv_style_set_border_width(&styleKitListBox, 3);
    lv_style_set_border_color(&styleKitListBox, CROSSPAD_THEME_PRIMARY);
    lv_style_set_border_opa(&styleKitListBox, LV_OPA_COVER);

    // kit list item style
    lv_style_init(&styleKitListItem);
    lv_style_set_bg_color(&styleKitListItem, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_color(&styleKitListItem, TEXT_COLOR_ON_PRIMARY);
    lv_style_set_text_font(&styleKitListItem, &lv_font_montserrat_18);
    lv_style_set_radius(&styleKitListItem, 8);
    lv_style_set_pad_all(&styleKitListItem, 4);
    lv_style_set_outline_width(&styleKitListItem, 0);

    // kit list item focused style
    lv_style_init(&styleKitListItemFocused);
    lv_style_set_bg_color(&styleKitListItemFocused, CROSSPAD_THEME_PRIMARY);
    lv_style_set_outline_width(&styleKitListItemFocused, 2);
    lv_style_set_outline_color(&styleKitListItemFocused, CROSSPAD_THEME_ACCENT);
    lv_style_set_pad_all(&styleKitListItemFocused, 2);

    // kit list item selected style
    lv_style_init(&styleKitListItemSelected);
    lv_style_set_bg_color(&styleKitListItemSelected, CROSSPAD_THEME_SECONDARY);
    lv_style_set_text_color(&styleKitListItemSelected, TEXT_COLOR_ON_SECONDARY);

    // kit list scroll style
    lv_style_init(&styleKitListScroll);
    lv_style_set_bg_color(&styleKitListScroll, lv_color_black());
    lv_style_set_radius(&styleKitListScroll, 0);
    lv_style_set_border_width(&styleKitListScroll, 2);
    lv_style_set_border_color(&styleKitListScroll, CROSSPAD_THEME_PRIMARY);
    lv_style_set_pad_all(&styleKitListScroll, 5);
    lv_style_set_pad_right(&styleKitListScroll, 0);
    lv_style_set_pad_gap(&styleKitListScroll, 0);
    lv_style_set_outline_width(&styleKitListScroll, 0);

    // loading arc style
    lv_style_init(&styleLoadingArc);
    lv_style_set_border_color(&styleLoadingArc, TEXT_COLOR_ON_PRIMARY);
    lv_style_set_border_width(&styleLoadingArc, 0);
    lv_style_set_radius(&styleLoadingArc, 1);
    lv_style_set_pad_all(&styleLoadingArc, 2);

    // spinbox style
    lv_style_init(&styleSpinbox);
    lv_style_set_bg_opa(&styleSpinbox, LV_OPA_TRANSP);
    lv_style_set_border_side(&styleSpinbox, LV_BORDER_SIDE_NONE);
    lv_style_set_border_width(&styleSpinbox, 0);
    lv_style_set_outline_width(&styleSpinbox, 0);
    lv_style_set_pad_all(&styleSpinbox, 0);

    // file explorer list style
    lv_style_init(&styleFileExplorerList);
    lv_style_set_bg_color(&styleFileExplorerList, CROSSPAD_THEME_PRIMARY);
    lv_style_set_border_width(&styleFileExplorerList, 1);
    lv_style_set_border_color(&styleFileExplorerList, lv_color_black());
    lv_style_set_pad_all(&styleFileExplorerList, 0);
    lv_style_set_clip_corner(&styleFileExplorerList, false);
    lv_style_set_radius(&styleFileExplorerList, 0);
    lv_style_set_text_line_space(&styleFileExplorerList, 2);

    // file explorer entry style
    lv_style_init(&styleFileExplorerEntry);
    lv_style_set_bg_opa(&styleFileExplorerEntry, LV_OPA_TRANSP);
    lv_style_set_text_color(&styleFileExplorerEntry, TEXT_COLOR_ON_PRIMARY);
    lv_style_set_text_font(&styleFileExplorerEntry, &lv_font_montserrat_14);
    lv_style_set_text_line_space(&styleFileExplorerEntry, 0);
    lv_style_set_border_width(&styleFileExplorerEntry, 0);
    lv_style_set_border_color(&styleFileExplorerEntry, lv_color_black());

    // file explorer entry selected style
    lv_style_init(&styleFileExplorerEntrySelected);
    lv_style_set_bg_opa(&styleFileExplorerEntrySelected, LV_OPA_COVER);
    lv_style_set_bg_color(&styleFileExplorerEntrySelected, CROSSPAD_THEME_ACCENT);
    lv_style_set_border_side(&styleFileExplorerEntrySelected, lv_border_side_t(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM));
    lv_style_set_border_width(&styleFileExplorerEntrySelected, 2);
    lv_style_set_border_opa(&styleFileExplorerEntrySelected, LV_OPA_100);

    // file explorer scrollbar style
    lv_style_init(&styleFileExplorerScrollbar);
    lv_style_set_bg_color(&styleFileExplorerScrollbar, lv_color_black());
    lv_style_set_border_width(&styleFileExplorerScrollbar, 1);
    lv_style_set_border_color(&styleFileExplorerScrollbar, lv_color_black());
    lv_style_set_border_opa(&styleFileExplorerScrollbar, LV_OPA_100);

    // settings category row style
    lv_style_init(&styleSettingsRow);
    lv_style_set_bg_color(&styleSettingsRow, lv_color_make(20, 20, 20));
    lv_style_set_bg_opa(&styleSettingsRow, LV_OPA_COVER);
    lv_style_set_border_side(&styleSettingsRow, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_width(&styleSettingsRow, 3);
    lv_style_set_border_color(&styleSettingsRow, CROSSPAD_THEME_PRIMARY);
    lv_style_set_radius(&styleSettingsRow, 8);
    lv_style_set_pad_hor(&styleSettingsRow, 10);
    lv_style_set_pad_ver(&styleSettingsRow, 6);
    lv_style_set_text_color(&styleSettingsRow, lv_color_white());
    lv_style_set_text_font(&styleSettingsRow, &lv_font_montserrat_16);

    // settings row focused state
    lv_style_init(&styleSettingsRowFocused);
    lv_style_set_outline_width(&styleSettingsRowFocused, 2);
    lv_style_set_outline_color(&styleSettingsRowFocused, CROSSPAD_THEME_PRIMARY);
    lv_style_set_bg_color(&styleSettingsRowFocused, lv_color_make(35, 35, 35));

    // settings header style
    lv_style_init(&styleSettingsHeader);
    lv_style_set_bg_color(&styleSettingsHeader, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_color(&styleSettingsHeader, lv_color_black());
    lv_style_set_text_font(&styleSettingsHeader, &lv_font_montserrat_14);
    lv_style_set_pad_all(&styleSettingsHeader, 6);
    lv_style_set_radius(&styleSettingsHeader, 6);

    // settings section header style
    lv_style_init(&styleSettingsSection);
    lv_style_set_text_color(&styleSettingsSection, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_font(&styleSettingsSection, &lv_font_montserrat_14);
    lv_style_set_pad_top(&styleSettingsSection, 8);
    lv_style_set_pad_bottom(&styleSettingsSection, 2);
    lv_style_set_pad_left(&styleSettingsSection, 4);
}

void deinitStyles() {
    lv_style_reset(&styleStatusBar);
    lv_style_reset(&styleAppContainer);
    lv_style_reset(&styleAppPicker);
    lv_style_reset(&styleAppPickerPressed);
    lv_style_reset(&styleAppPickerInactive);
    lv_style_reset(&styleButton);
    lv_style_reset(&styleButtonPressed);
    lv_style_reset(&styleButtonInactive);
    lv_style_reset(&styleTitle);
    lv_style_reset(&styleBackButton);
    lv_style_reset(&stylePrimaryImgRecolor);
    lv_style_reset(&styleWaveformBox);
    lv_style_reset(&styleKitListBox);
    lv_style_reset(&styleKitListScroll);
    lv_style_reset(&styleKitListItem);
    lv_style_reset(&styleKitListItemFocused);
    lv_style_reset(&styleKitListItemSelected);
    lv_style_reset(&styleLoadingArc);
    lv_style_reset(&styleSpinbox);
    lv_style_reset(&styleFileExplorerList);
    lv_style_reset(&styleFileExplorerEntry);
    lv_style_reset(&styleFileExplorerEntrySelected);
    lv_style_reset(&styleFileExplorerScrollbar);
    lv_style_reset(&styleSettingsRow);
    lv_style_reset(&styleSettingsRowFocused);
    lv_style_reset(&styleSettingsHeader);
    lv_style_reset(&styleSettingsSection);
}

#endif // USE_LVGL
