#ifndef LV_CONF_H
#define LV_CONF_H

/* What crosspad-gui's sources need (its fonts); everything else takes LVGL's defaults. */
#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (1024 * 1024)
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif
