// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file fw_store_view.h
 * @brief The Store tab: the catalog's kits as tiles, Official then Community,
 *        the version each tile offers in its kit's channel, a check mark when
 *        that exact version is on the card, download progress on its tile.
 *        Shown, it holds the views' WiFi lease and checks the catalog (once a
 *        minute at most); without WiFi it shows the catalog cached on the card
 *        and why. Hosted by the Firmware app and by the network-mode screen.
 *        One instance; LVGL thread only.
 */

#ifdef USE_LVGL
#include "lvgl.h"

void fw_store_view_build(lv_obj_t *parent, lv_group_t *group);
void fw_store_view_refresh(void);   /* the host's timer, every 250 ms */
void fw_store_view_shown(void);     /* the tab became visible: lease, check */
void fw_store_view_close(void);     /* before the host deletes `parent`; the lease goes */

#endif // USE_LVGL
