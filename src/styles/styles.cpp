#ifdef USE_LVGL

#include "crosspad-gui/styles/styles.h"
#include "crosspad-gui/theme/crosspad_theme.h"
#include "crosspad-gui/widgets/gui_helpers.h"
#include "crosspad/settings/CrosspadSettings.hpp"
#include "themes/lv_theme_private.h"

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
lv_style_t styleKitBrowserScreen;
lv_style_t styleKitBrowserStatus;
lv_style_t styleKitBrowserRow;
lv_style_t styleKitBrowserRowSelected;
lv_style_t styleKitBrowserCover;
lv_style_t styleKitBrowserThumb;
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

// Native slider focus/edit contrast (see installSliderContrastTheme() below):
// the stock LVGL theme rings both the encoder-focus and edit states in shades
// of the same hue as the knob, so on this small screen "which slider has
// focus" and "which one is armed for editing" read as the same blue smear.
static lv_style_t styleSliderFocusRing;
static lv_style_t styleSliderEditRing;
static lv_style_t styleSliderEditKnob;
static void installSliderContrastTheme();

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

    // kit browser: the whole screen
    lv_style_init(&styleKitBrowserScreen);
    lv_style_set_bg_color(&styleKitBrowserScreen, CROSSPAD_THEME_BG_DEEP);
    lv_style_set_bg_opa(&styleKitBrowserScreen, LV_OPA_COVER);
    lv_style_set_pad_all(&styleKitBrowserScreen, 0);
    lv_style_set_border_width(&styleKitBrowserScreen, 0);
    lv_style_set_radius(&styleKitBrowserScreen, 0);

    // kit browser: status bar. A dark bar with the accent spent on a hairline
    // — a full primary slab reads as a neon stripe and pulls the eye off the
    // artwork, which is what the screen is for.
    lv_style_init(&styleKitBrowserStatus);
    lv_style_set_bg_color(&styleKitBrowserStatus, CROSSPAD_THEME_CARD_DEEP);
    lv_style_set_bg_opa(&styleKitBrowserStatus, LV_OPA_COVER);
    lv_style_set_text_color(&styleKitBrowserStatus, CROSSPAD_THEME_TEXT);
    lv_style_set_text_font(&styleKitBrowserStatus, &lv_font_montserrat_14);
    lv_style_set_radius(&styleKitBrowserStatus, 0);
    lv_style_set_border_width(&styleKitBrowserStatus, 1);
    lv_style_set_border_side(&styleKitBrowserStatus, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&styleKitBrowserStatus, CROSSPAD_THEME_PRIMARY);
    lv_style_set_border_opa(&styleKitBrowserStatus, LV_OPA_40);
    lv_style_set_pad_hor(&styleKitBrowserStatus, 9);
    lv_style_set_pad_ver(&styleKitBrowserStatus, 0);

    // kit browser: one row per kit
    lv_style_init(&styleKitBrowserRow);
    lv_style_set_bg_color(&styleKitBrowserRow, CROSSPAD_THEME_CARD_DEEP);
    lv_style_set_bg_opa(&styleKitBrowserRow, LV_OPA_COVER);
    lv_style_set_text_color(&styleKitBrowserRow, CROSSPAD_THEME_TEXT);
    lv_style_set_radius(&styleKitBrowserRow, 9);
    lv_style_set_border_width(&styleKitBrowserRow, 0);
    lv_style_set_pad_hor(&styleKitBrowserRow, 7);
    lv_style_set_pad_ver(&styleKitBrowserRow, 0);
    lv_style_set_pad_column(&styleKitBrowserRow, 7);

    // kit browser: the selected row
    lv_style_init(&styleKitBrowserRowSelected);
    lv_style_set_bg_color(&styleKitBrowserRowSelected, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_color(&styleKitBrowserRowSelected, TEXT_COLOR_ON_PRIMARY);

    // kit browser: cover panel (clips the artwork to rounded corners)
    lv_style_init(&styleKitBrowserCover);
    lv_style_set_radius(&styleKitBrowserCover, 14);
    lv_style_set_clip_corner(&styleKitBrowserCover, true);
    lv_style_set_bg_color(&styleKitBrowserCover, CROSSPAD_THEME_CARD_DEEP);
    lv_style_set_bg_opa(&styleKitBrowserCover, LV_OPA_COVER);
    lv_style_set_text_color(&styleKitBrowserCover, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_font(&styleKitBrowserCover, &lv_font_montserrat_18);

    // kit browser: row thumbnail
    lv_style_init(&styleKitBrowserThumb);
    lv_style_set_radius(&styleKitBrowserThumb, 5);
    lv_style_set_clip_corner(&styleKitBrowserThumb, true);
    lv_style_set_bg_color(&styleKitBrowserThumb, CROSSPAD_THEME_BG_DEEP);
    lv_style_set_bg_opa(&styleKitBrowserThumb, LV_OPA_COVER);
    lv_style_set_text_color(&styleKitBrowserThumb, CROSSPAD_THEME_PRIMARY);
    lv_style_set_text_font(&styleKitBrowserThumb, &lv_font_montserrat_10);

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
    lv_style_set_text_font(&styleSettingsRow, &lv_font_montserrat_14);

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

    // slider focus ring: same accent used for focus everywhere else in the
    // app, but wider and opaque instead of the stock theme's 50%-opacity
    // sliver, so the ring is visible against the knob it surrounds.
    lv_style_init(&styleSliderFocusRing);
    lv_style_set_outline_color(&styleSliderFocusRing, CROSSPAD_THEME_PRIMARY);
    lv_style_set_outline_width(&styleSliderFocusRing, 5);
    lv_style_set_outline_pad(&styleSliderFocusRing, 4);
    lv_style_set_outline_opa(&styleSliderFocusRing, LV_OPA_90);

    // slider edit ring/knob: a fixed accent unrelated to the (user-picked)
    // primary color, so "armed for editing" never blends into "focused".
    lv_style_init(&styleSliderEditRing);
    lv_style_set_outline_color(&styleSliderEditRing, CROSSPAD_THEME_ACCENT);
    lv_style_set_outline_width(&styleSliderEditRing, 5);
    lv_style_set_outline_pad(&styleSliderEditRing, 4);
    lv_style_set_outline_opa(&styleSliderEditRing, LV_OPA_COVER);

    lv_style_init(&styleSliderEditKnob);
    lv_style_set_bg_color(&styleSliderEditKnob, CROSSPAD_THEME_ACCENT);
    lv_style_set_bg_opa(&styleSliderEditKnob, LV_OPA_COVER);

    installSliderContrastTheme();
}

// ── Slider focus/edit contrast (theme extension) ────────────────────────────
// LVGL's default theme applies the same styles to every lv_slider_class
// instance from one apply_cb; the documented way to add to that without
// re-deriving the whole theme is to chain onto it (see lv_theme_private.h).
static lv_theme_apply_cb_t s_baseThemeApplyCb = nullptr;

static void crosspadSliderThemeApplyCb(lv_theme_t * th, lv_obj_t * obj) {
    if (s_baseThemeApplyCb) s_baseThemeApplyCb(th, obj);

    if (lv_obj_check_type(obj, &lv_slider_class)) {
        lv_obj_add_style(obj, &styleSliderFocusRing, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(obj, &styleSliderEditRing, LV_STATE_EDITED);
        lv_obj_add_style(obj, &styleSliderEditKnob, (lv_style_selector_t)LV_PART_KNOB | (lv_style_selector_t)LV_STATE_EDITED);
    }
}

static void installSliderContrastTheme() {
    lv_theme_t * theme = lv_theme_default_get();
    if (!theme) return;
    if (theme->apply_cb != crosspadSliderThemeApplyCb) {
        s_baseThemeApplyCb = theme->apply_cb;
        theme->apply_cb = crosspadSliderThemeApplyCb;
    }
}

void reinitStyles() {
    deinitStyles();
    initStyles();
}

// ── Runtime theme color ─────────────────────────────────────────────────────

namespace crosspad_gui {

lv_color_t getThemePrimary() {
    auto* s = crosspad::CrosspadSettings::getInstance();
    uint8_t idx;
    if (s && s->themeColorIndex < THEME_PALETTE_COUNT) {
        idx = s->themeColorIndex;
    } else {
        idx = CROSSPAD_THEME_DEFAULT_INDEX < THEME_PALETTE_COUNT
              ? CROSSPAD_THEME_DEFAULT_INDEX : 0;
    }
    return lv_color_make(s_theme_palette[idx].r,
                         s_theme_palette[idx].g,
                         s_theme_palette[idx].b);
}

int getThemeColorCount() {
    return THEME_PALETTE_COUNT;
}

const char* getThemeColorName(int idx) {
    if (idx >= 0 && idx < THEME_PALETTE_COUNT)
        return s_theme_palette[idx].name;
    return "";
}

} // namespace crosspad_gui

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
    lv_style_reset(&styleKitBrowserScreen);
    lv_style_reset(&styleKitBrowserStatus);
    lv_style_reset(&styleKitBrowserRow);
    lv_style_reset(&styleKitBrowserRowSelected);
    lv_style_reset(&styleKitBrowserCover);
    lv_style_reset(&styleKitBrowserThumb);
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
    lv_style_reset(&styleSliderFocusRing);
    lv_style_reset(&styleSliderEditRing);
    lv_style_reset(&styleSliderEditKnob);
}

#endif // USE_LVGL
