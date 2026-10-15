// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef USE_LVGL
#include "lvgl.h"
#include "crosspad-gui/fw/FwCardActions.hpp"
#include "crosspad-gui/fw/IFwViewService.h"

/**
 * @file fw_actions.h
 * @brief What a kit card's button (or an Updates row) does, with the
 *        questions it needs: the community warning before a community kit's
 *        first install, "older version?" before installing one, "replace the
 *        other firmware?" when the other slot holds another kit, the install
 *        and switch confirmations, and "run the new version now?" after a
 *        download-and-install. LVGL thread only.
 */

struct FwTarget {
    char kit[crosspad::kFwKitMax];
    char version[crosspad::kFwPkgVersionMax];   /* the catalog's version, or the image's without its 'v' */
    char pkg[crosspad::kFwPackageNameMax];          /* the package directory; "" when not on the card */
    char slot;                   /* 'A' / 'B': the slot image a Library tile showed, its package not on the card */
    bool community;
    bool unknownBuild;           /* an image without CPFI: installed only after its warning */
    bool stmProtoOlder;          /* built for an older pad-controller protocol: said in the confirm */
};

const char *fw_action_label(crosspad_gui::FwCardAct act);

/** `tab`: where a no_mem refusal offers network mode; `group`: where a dialog gives the encoder back. */
void fw_action_start(crosspad_gui::FwCardAct act, const FwTarget &t, crosspad_gui::FwNetTab tab, lv_group_t *group);

/** A press the updater refused at once, `why` its refusal token: "Busy", the
 *  reason in words, or -- no_mem -- the offer of network mode. */
void fw_action_refused(const char *why, crosspad_gui::FwNetTab tab, lv_group_t *group);

/** A user's check is running: a no_mem outcome (the radio's gate after the lease) offers network mode once. */
void fw_actions_track_check(crosspad_gui::FwNetTab tab);

/** From fw_views_poll(), whichever tab is shown: settles a press whose own job ended. */
void fw_actions_tick(void);

/** The host goes (the Firmware app's or the network-mode screen's teardown, never
 *  a tab switch): the pending press is dropped and an open confirm is answered
 *  "no", so nothing acts on a closed app. */
void fw_actions_forget(void);

#endif // USE_LVGL
