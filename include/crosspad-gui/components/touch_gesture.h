#pragma once

/**
 * @file touch_gesture.h
 * @brief Screen-wide swipes, recognised below LVGL's object layer.
 *
 * LVGL has a gesture detector and it cannot be used for this: indev_gesture()
 * returns early on pointer.scroll_obj, and on this UI the launcher, every list
 * and every settings page is scrollable — the swipe would work only on the few
 * screens that scroll nothing. So the samples are taken by wrapping the touch
 * indev's read_cb, where nothing can swallow them.
 *
 * Thresholds come from 20 gestures recorded on the real panel (TOUCH_TRACE,
 * 9.09): swipes travel 117–247 px on the dominant axis with at most 26 px of
 * cross-axis drift, last 175–436 ms, and taps move 0 px. The panel is sampled
 * every ~13 ms, so a swipe is 12–35 points and needs no extrapolation.
 */

#ifdef USE_LVGL

#include "lvgl.h"

namespace crosspad_gui {

enum class SwipeDir : uint8_t { None, Right, Left, Down, Up };

/// Thresholds, in panel pixels and milliseconds. Defaults are the measured
/// ones with margin; a platform may narrow them but should not have to.
struct SwipeConfig {
    int      minTravel = 60;    ///< dominant axis; taps move 0, real swipes ≥117
    int      crossRatio = 2;    ///< dominant ≥ this × cross; measured ratios are ≥7
    uint32_t maxMs      = 600;  ///< longer is a drag, not a swipe (longest measured 436)
    /**
     * A downward swipe opens the quick-settings drawer only when it starts in
     * this band below the top edge. Everywhere else a downward drag is a list
     * scroll, and the two are geometrically identical — the kit selector, the
     * pad editor and the pitched editor all scroll vertically. The band is the
     * top bar plus room for a fingertip: 18 px below the 22 px status bar it
     * was drawn for, and as much below the 30 px transport bar that replaced
     * it -- a band left at 40 kept 10, and fingers on the board started a
     * pull at y = 53 and 67 and got nothing.
     */
    int topBand = 48;
    /**
     * Where on the pointer's plane a swipe may start, and the edge topBand
     * counts from. The whole plane on the board, whose panel is the screen;
     * the simulator draws the panel inside a picture of the device and
     * passes the LCD's rectangle, so a drag across the drawn pads is not a
     * back gesture.
     */
    lv_area_t area = { 0, 0, LV_COORD_MAX, LV_COORD_MAX };
};

/// Wrap the pointer indev. Safe with nullptr (a board whose panel never came
/// up); calling twice is a no-op.
void touch_gesture_attach(lv_indev_t* indev, const SwipeConfig& cfg = SwipeConfig{});

/// What the last recognised swipe was, and how many have been acted on — the
/// only way a bench can tell "the gesture was not recognised" from "it was
/// recognised and the action did nothing".
SwipeDir touch_gesture_last();
uint32_t touch_gesture_count();
const char* swipeDirName(SwipeDir d);

/// How the last leftward swipe resolved: "platform" (it switched screens),
/// "nothing" (recognised, nowhere to go), or "-" if none has happened. A swipe
/// that silently does nothing is otherwise indistinguishable from one that was
/// never recognised at all.
const char* touch_gesture_forward_result();

/**
 * The leftward swipe, resolved entirely by the platform: switch to the other of
 * the two screens most recently used, the way a double-tap on Android's recents
 * button does. Return true when it went somewhere.
 *
 * There is deliberately no fallback here. Only the platform knows every kind of
 * screen — the sampler's kit selector is one the launcher never started — and a
 * second rule in this layer disagreed with it about where the swipe lands.
 */
using RecentsFn = bool (*)();
void touch_gesture_set_recents(RecentsFn fn);

} // namespace crosspad_gui

#endif // USE_LVGL
