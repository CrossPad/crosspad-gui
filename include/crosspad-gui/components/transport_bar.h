// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/**
 * @file transport_bar.h
 * @brief The transport, always at the top of the screen.
 *
 * One 30 px row in place of the status bar, the same in every app:
 *
 *   [MODE] [● REC] [DUB] [▶] [■] [metronome]  BPM 120.0 | PLAY 002.3.08 | VOICES 5/10  [bat]
 *
 * It paints from crosspad::transportState() / transportCaps() /
 * transportName() and sends its keys to transportPlay() / Stop() / Record() /
 * Overdub(), so it never learns which app holds the claim. The colour of the
 * whole row is the state -- the theme colour stopped or playing, amber while
 * armed (Waiting), red while recording or overdubbing -- readable from across
 * a stage.
 *
 * VOICES is IGuiPlatform::getVoiceMeter(): sounding / the ceiling that
 * applies, always on -- dim with nothing sounding, "--" with no kit. At the
 * ceiling it inverts, because the next hit steals a voice and that is audible.
 *
 * BPM and the position come from the claim holder (transportBpm(),
 * transportPosition()); BPM reads "--" when it has no tempo of its own.
 * Both are also values the encoder turns: BPM is the instrument's one tempo
 * (transportSetTempo() while it turns, transportCommitTempo() when it is let
 * go, refused while a host's MIDI clock drives it), the position moves where
 * PLAY starts a bar a detent while stopped (transportLocate(), a holder with
 * kTransportCanLocate -- Arrange).
 *
 * The metronome key is the click's one switch (transportClick(),
 * transportSetClick()), lit while it clicks; the Sequencer's M is the same
 * switch, and no transport command flips it.
 *
 * The status bar is still built, with its icons, loader and timers running,
 * as the second page of one row beside this bar: a finger drags the row
 * sideways and it snaps to whichever bar it was dragged to (a flick, or past
 * half way). Tapping the battery swaps the two as well, and so does the
 * encoder's click on it -- it is the last key in the rotation -- with the
 * status bar's arrow, then in the rotation in place of the keys, swapping
 * back. A downward swipe from the top edge opens the quick-settings drawer
 * under either page. The loader runs as a 2 px line along the bar's bottom
 * edge.
 *
 * The keys sit in whatever group the encoder is on, appended, so turning one
 * step past the end of any screen reaches MODE / REC / DUB / PLAY / STOP /
 * metronome / BPM / position / battery. The power button's 0.5 s hold is the shortcut there.
 *
 * All functions must be called from the LVGL task.
 */
#ifdef USE_LVGL
#include "lvgl.h"

#ifndef TRANSPORT_BAR_HEIGHT
#define TRANSPORT_BAR_HEIGHT 30
#endif

namespace crosspad_gui {

/// Build the bar as a child of @p parent, visible, and own it from here.
/// @p status_bar is moved into the bar's row as its second page, where it
/// was in @p parent, and made as tall as the bar; nullptr disables the swap.
lv_obj_t * transportbar_create(lv_obj_t * parent, lv_obj_t * status_bar);

/// Repaint from the core transport now rather than on the next tick.
void transportbar_refresh();

/// What a MODE tap calls. mode_overlay_init() installs mode_overlay_toggle().
/// A long press on MODE is the way to the status bar instead (see
/// transportbar_show) -- this design has no room for an arrow, and the wifi,
/// Bluetooth, SD and loader glyphs live nowhere else.
void transportbar_set_mode_handler(void (*handler)());

/// Swap with the status bar, animated. show=false reveals the icons,
/// show=true brings the transport back. No-op without a status bar to swap
/// with. transportbar_shown() answers for the page asked for at once, and
/// for the one a drag came to rest on.
void transportbar_show(bool show);
/// True while the transport bar (not the status bar) is the one shown.
bool transportbar_shown();

/// One of the bar's readouts, for measuring the layout from the host.
struct TransportBarReadout {
    const char * id;     ///< "bpm", "pos" or "vox"
    int32_t      x, y, w, h;   ///< the label's area on screen, in pixels
    const char * text;   ///< the label's own buffer; valid until the next repaint
};

/// Fill up to @p max readouts and return how many were written: where each
/// label sits and what it shows, for checking the row's layout from the
/// host. These labels sit in no encoder group, so a UI snapshot does not
/// list them.
uint8_t transportbar_probe(TransportBarReadout * out, uint8_t max);

/// Where the row of the two bars rests: 0 on the transport, the row's width
/// on the status icons, anything between while a finger drags it or it
/// snaps. -1 without a bar. For the host, like transportbar_probe().
int32_t transportbar_row_scroll();

/// Jump the encoder's focus to the transport, and a second call back to
/// where it came from -- what the power button's 0.5 s hold does.
///
/// A shortcut, not the way in: the keys are appended to the encoder's group,
/// so turning past a screen's last field reaches them anyway. LVGL skips
/// hidden and LV_STATE_DISABLED objects when it navigates, so a hidden bar and
/// a key the claim holder cannot honour drop out of the rotation on their own.
/// The jump lands on PLAY when stopped, STOP otherwise. Back from keys the
/// encoder was turned onto lands on the screen's last control, the one
/// turning past it came from.
///
/// False when there is no bar, or it is not the one on screen, or the screen
/// has nothing of its own to take the focus back.
bool transportbar_toggle_focus();

/// True while the encoder's focus is on one of the bar's keys or values.
bool transportbar_has_focus();

} // namespace crosspad_gui

#endif // USE_LVGL
