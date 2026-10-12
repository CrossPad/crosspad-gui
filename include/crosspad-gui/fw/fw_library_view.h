// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file fw_library_view.h
 * @brief The Library tab: every package on the SD card as a kit tile, the
 *        slots' packages first, and the card's use against the library limit.
 *        One instance; LVGL thread only.
 */

#ifdef USE_LVGL
#include "lvgl.h"

void fw_library_view_build(lv_obj_t *parent, lv_group_t *group);
void fw_library_view_refresh(void);   /* the host's timer, every 250 ms */
void fw_library_view_close(void);     /* before the host deletes `parent` */

#endif // USE_LVGL
