#pragma once

#ifdef USE_LVGL
#include "lvgl.h"

// Global style objects - initialized by initStyles(), freed by deinitStyles()
extern lv_style_t styleStatusBar;
extern lv_style_t styleAppContainer;
extern lv_style_t styleAppPicker;
extern lv_style_t styleAppPickerPressed;
extern lv_style_t styleAppPickerInactive;
extern lv_style_t styleButton;
extern lv_style_t styleButtonPressed;
extern lv_style_t styleButtonInactive;
extern lv_style_t styleTitle;
extern lv_style_t styleBackButton;
extern lv_style_t stylePrimaryImgRecolor;
extern lv_style_t styleWaveformBox;
extern lv_style_t styleKitListBox;
extern lv_style_t styleKitListScroll;
extern lv_style_t styleKitListItem;
extern lv_style_t styleKitListItemFocused;
extern lv_style_t styleKitListItemSelected;
extern lv_style_t styleLoadingArc;
extern lv_style_t styleSpinbox;

// File explorer
extern lv_style_t styleFileExplorerList;
extern lv_style_t styleFileExplorerEntry;
extern lv_style_t styleFileExplorerEntrySelected;
extern lv_style_t styleFileExplorerScrollbar;

// Settings
extern lv_style_t styleSettingsRow;
extern lv_style_t styleSettingsRowFocused;
extern lv_style_t styleSettingsHeader;
extern lv_style_t styleSettingsSection;

void initStyles();
void deinitStyles();

#endif // USE_LVGL
