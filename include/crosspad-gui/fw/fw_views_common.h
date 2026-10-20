// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef USE_LVGL
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "crosspad-gui/components/ui_tile_grid.h"
#include "crosspad-gui/fw/IFwViewService.h"

/**
 * @file fw_views_common.h
 * @brief What the Firmware views share (fw_*_view.cpp, fw_kit_card.cpp,
 *        fw_actions.cpp): the kit tile and its icon, texts, the slots, the
 *        poll every view's refresh starts with. LVGL thread only.
 */

constexpr int32_t kFwPad = 4;
constexpr uint32_t kFwToastMs = 2500;

struct FwTileView {
    const char *name;
    const char *meta;   /* version and badges, or why it cannot be used */
    bool dim;
};

struct FwIconKey {
    char pkg[crosspad::kFwPackageNameMax];
    char kit[crosspad::kFwKitMax];
    char version[crosspad::kFwPkgVersionMax];
};

/** The icon files of item `index`; false: it has none (the tile keeps its letter). */
using FwIconFor = bool (*)(uint32_t index, FwIconKey *key);

/** A view's grid: 3 tiles a row, two rows filling `height`. cfg's fill,
 *  clicked, selected, window, user and emptyText are the caller's; the tile
 *  (create, unused) and the geometry are set here. */
lv_obj_t *fw_grid_create(lv_obj_t *parent, lv_group_t *group, crosspad_gui::TileGridConfig cfg, int32_t height);

/** Shows an item in a tile; an icon of another item is let go first. */
void fw_tile_show(lv_obj_t *tile, uint32_t index, const FwTileView &v);

/** Icons for the tiles at least half in view, none for the rest (the PSRAM budget).
 *  Cheap when nothing changed: views call it on every refresh, so a request the
 *  worker's full queue refused is asked again. */
void fw_tile_icons_sync(lv_obj_t *grid, FwIconFor iconFor);

/** Before a grid goes: images detached from their buffers, references released. */
void fw_tile_icons_release(lv_obj_t *grid);

const lv_style_t *fw_views_text_style(void);   /* montserrat_10 in the theme's text colour */

void fw_letter_of(const char *name, char *out, size_t n);        /* first character, ASCII upper-cased */
void fw_format_mb(uint64_t bytes, char *out, size_t n);          /* "2.6 MB" */
void fw_format_date(int64_t unixSeconds, char *out, size_t n);   /* "2026-10-02", "-" for 0 */
const char *fw_strip_v(const char *version);

/** [0] = slot A, [1] = slot B. Re-read (readSlots(), flash) after the
 *  updater or an install/switch flow finished; never on a worker. */
const crosspad::FwSlotInfo *fw_views_slots(void);
/** The next fw_views_slots() reads the table again: an install over the
 *  platform's own console moves a slot without either of those transitions. */
void fw_views_slots_invalidate(void);
char fw_views_slot_of(const char *pkg, bool *running);   /* 'A' / 'B', or 0 */

/** The first object of `scope` (depth first) in `group`; focuses it. */
bool fw_views_focus_first(lv_obj_t *scope, lv_group_t *group);

/** A view starts / stops: styles, the data, icons. */
void fw_views_acquire(void);
void fw_views_release(void);

/** Every view's refresh calls this first; it runs once per LVGL tick however
 *  many views call it: the data's answers move in, and an updater job or a
 *  flow that just ended rescans the card and reloads the catalog. */
void fw_views_poll(void);

/** Changes whenever an updater job or a flow ended (views redraw their state). */
uint32_t fw_views_updater_gen(void);

/** A view started an updater job: its end moves the generation even when it
 *  ends before the next poll has seen it running. */
void fw_views_job_started(void);

/** A Store or Updates tab became visible: the views' WiFi lease is taken if
 *  not held (a refusal is shown as the reason; no_mem offers network mode on
 *  `tab`, once for this showing), and the catalog is checked when the last
 *  automatic check is a minute old and the lease is held. Nothing is offered
 *  in network mode. */
void fw_views_tab_shown(crosspad_gui::FwNetTab tab, lv_group_t *group);

/** That tab closed: the lease goes with the last tab holding it. */
void fw_views_tab_closed(crosspad_gui::FwNetTab tab);

/** "Check now": a shown tab's refused lease is asked for again, then it always
 *  checks; a refusal is offered/toasted like a press. True: a check started. */
bool fw_views_check_now(crosspad_gui::FwNetTab tab, lv_group_t *group);

/** Why the views' lease is not up, or why the tab's automatic check was refused
 *  (until a press or a job): "" when neither (or still connecting), else an
 *  updater error token (fw_updates_error_text() says it in words). */
const char *fw_views_wifi_reason(void);

/** A view needs the cached catalog: the first user loads it, the last drops it. */
void fw_views_want_store(bool want);

#endif // USE_LVGL
