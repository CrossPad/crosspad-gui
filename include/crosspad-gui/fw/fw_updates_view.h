// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file fw_updates_view.h
 * @brief The Updates tab: the state of the catalog check, Check now, Run the
 *        new version, one row per kit on the card (or in a slot) with a newer
 *        version in its channel and *Update* (download, install, run now?),
 *        and in a normal boot a shortcut to Settings > Updates. Shown, it holds
 *        the views' WiFi lease and checks (once a minute at most). Hosted by
 *        the Firmware app and by the network-mode screen. One instance; LVGL
 *        thread only. (B's minimal view had these entry points; C filled it.)
 */

#ifdef USE_LVGL
#include <stddef.h>
#include "lvgl.h"

void fw_updates_view_build(lv_obj_t *parent, lv_group_t *group);
void fw_updates_view_refresh(void);   /* the host's timer, every few hundred ms */
void fw_updates_view_shown(void);     /* the tab became visible: lease, check */
void fw_updates_view_close(void);     /* before the host deletes `parent`; the lease goes */

/** Focusables the host put after the view (the Over USB block): kept behind
 *  the update rows in encoder order when the rows are rebuilt. */
void fw_updates_view_set_tail(lv_obj_t *const *objs, size_t n);

/** An updater error token in words for the user ("no_network" -> "no WiFi
 *  network in reach"); an unknown token reads as it is. Shared with Settings. */
const char *fw_updates_error_text(const char *err);

/** The token is "no_mem" or "no_mem:<where>": a restart in network mode frees it. */
bool fw_updates_is_no_mem(const char *err);

/** A wrapped status text above the focused control changed height: scroll that
 *  control fully into view again. Shared with Settings. LVGL thread only. */
void fw_updates_keep_focus_in_view(lv_group_t *group);

#endif // USE_LVGL
