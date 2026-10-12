// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef USE_LVGL
#include "lvgl.h"
#include "crosspad-gui/fw/fw_actions.h"

/**
 * @file fw_kit_card.h
 * @brief The full-screen kit card over a Library or Store tile: icon, name,
 *        version and badges, description, app chips, size - channel - tier -
 *        author, What's new, the actions the state allows (crosspad_gui::fwCardPlan)
 *        and the older versions. A power click (or Back) returns to the tab.
 *        One card at a time; LVGL thread only.
 */

/** Opens one LVGL cycle later (safe from a tile's click). A card already open is replaced. */
void fw_kit_card_open(const FwTarget &t, crosspad_gui::FwNetTab tab);

/** Closes the card, and an open still waiting for its LVGL cycle never happens:
 *  the host calls it before it goes. */
void fw_kit_card_close(void);
bool fw_kit_card_is_open(void);
void fw_kit_card_refresh(void);   /* from fw_views_poll() */

#endif // USE_LVGL
