// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef USE_LVGL
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"

/**
 * @file fw_icons.h
 * @brief Kit icons as LVGL images: 6924-byte
 *        buffers the platform reads (IFwViewService::requestIcon), shown as variable
 *        lv_image_dsc_t sources. LVGL draws an uncompressed RGB565A8 variable
 *        source in place and never puts it in its shared image cache. Files
 *        with the same bytes share one buffer; at most kIconsMax buffers
 *        (fw_icons.cpp), past that a tile keeps its letter. LVGL thread only.
 */

/** `icon` nullptr: neither file held a valid icon. */
using FwIconDone = void (*)(const lv_image_dsc_t *icon, void *user);

/** A reference to the icon already held for these files (a tile that showed
 *  them a moment ago), with no card read; nullptr when there is none. */
const lv_image_dsc_t *fw_icons_acquire(const char *pkg, const char *kit, const char *version);

/** <pkg>/icon.bin, else the card cache's <kit>-<version>.bin (either may be
 *  ""). `done` runs later from the service's dataPoll() with a reference the caller
 *  owns. 0 when it could not be queued: `done` is never called then. */
uint32_t fw_icons_request(const char *pkg, const char *kit, const char *version, FwIconDone done, void *user);

/** `done` will not be called for this ticket, and a request not read yet never is. */
void fw_icons_cancel(uint32_t ticket);

/** Let go of a reference. The buffer stays until fw_icons_trim(), so a tile
 *  refilled with the same item takes it back without a card read. */
void fw_icons_release(const lv_image_dsc_t *icon);

/** Buffers nobody holds are freed. */
void fw_icons_trim(void);

/** Every pending request forgotten and every unheld buffer freed (the last view closed). */
void fw_icons_drop_all(void);

/** The service's dataPoll(): the answer to `ticket`; `buf` (or nullptr) is handed
 *  over and goes back through IFwViewService::freeIcon(). */
void fw_icons_deliver(uint32_t ticket, uint8_t *buf);

#endif // USE_LVGL
