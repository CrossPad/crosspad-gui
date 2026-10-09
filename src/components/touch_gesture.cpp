#ifdef USE_LVGL

#include "crosspad-gui/components/touch_gesture.h"
#include "crosspad-gui/components/power_gesture.h"
#include "crosspad-gui/components/status_bar.h"

namespace crosspad_gui {

namespace {

SwipeConfig        s_cfg;
lv_indev_read_cb_t s_inner = nullptr;
lv_indev_t*        s_indev = nullptr;

/* One contact. `armed` goes false once the swipe has fired or has been ruled
 * out, so a single contact produces at most one action and the remainder of
 * the drag is not re-evaluated on every sample. */
struct Contact {
    bool     down    = false;
    bool     armed   = false;
    bool     fired   = false;
    int32_t  x0 = 0, y0 = 0;
    uint32_t t0      = 0;
} s_c;

lv_obj_t* s_pressed = nullptr;   // object under the press, for the widget veto
SwipeDir  s_last    = SwipeDir::None;
uint32_t  s_count   = 0;

RecentsFn   s_recent    = nullptr;
const char* s_fwdResult = "-";

/* Widgets that consume a horizontal drag themselves. A back swipe that fired
 * over the brightness slider would take the user out of Settings instead of
 * moving the slider, and the two gestures are the same motion.
 *
 * The veto is deliberately narrow. The first cut vetoed on any ancestor that
 * merely *allowed* horizontal scrolling, which is a flag half the containers
 * on a panel carry whether or not their content overflows — on the sampler
 * that made the swipe nearly impossible to land. A container only counts when
 * it actually has somewhere to scroll. */
bool consumesHorizontal(lv_obj_t* obj)
{
    for (lv_obj_t* o = obj; o; o = lv_obj_get_parent(o)) {
        if (lv_obj_check_type(o, &lv_slider_class) ||
            lv_obj_check_type(o, &lv_bar_class) ||
            lv_obj_check_type(o, &lv_arc_class)) {
            return true;
        }
#if LV_USE_ROLLER
        /* Nothing on the board creates a roller, and the firmware builds
         * LVGL without one (3.3 kB of image); a build that has it keeps the
         * veto. */
        if (lv_obj_check_type(o, &lv_roller_class)) return true;
#endif
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) &&
            (lv_obj_get_scroll_dir(o) & LV_DIR_HOR) &&
            (lv_obj_get_scroll_left(o) > 0 || lv_obj_get_scroll_right(o) > 0)) {
            return true;
        }
    }
    return false;
}

/* Runs one LVGL tick after recognition rather than inside the read: powerBack()
 * deletes objects, and the indev is mid-read when the swipe is spotted. */
void act(void* param)
{
    switch (static_cast<SwipeDir>((uintptr_t)param)) {
        case SwipeDir::Right:
            powerBack();
            break;
        case SwipeDir::Down:
            powerToggleQuickSettings();
            break;
        case SwipeDir::Left:
            /* Forward. The platform gets it first, because a screen the
             * launcher never started (the sampler's kit selector) has a
             * forward the generic rule cannot name. */
            /* One resolver, owned by the platform: it is the only layer that
             * knows every kind of screen. A second rule here — "re-enter the
             * last app the launcher started" — disagreed with it about where
             * the swipe should land, and which one won depended on the path
             * the screen had been reached by. */
            s_fwdResult = (s_recent && s_recent()) ? "platform" : "nothing";
            break;
        default:
            break;
    }
}

SwipeDir classify(int32_t dx, int32_t dy, uint32_t dt)
{
    if (dt > s_cfg.maxMs) return SwipeDir::None;

    const int32_t ax = dx < 0 ? -dx : dx;
    const int32_t ay = dy < 0 ? -dy : dy;

    if (ax >= s_cfg.minTravel && ax >= ay * s_cfg.crossRatio) {
        return dx > 0 ? SwipeDir::Right : SwipeDir::Left;
    }
    if (ay >= s_cfg.minTravel && ay >= ax * s_cfg.crossRatio) {
        return dy > 0 ? SwipeDir::Down : SwipeDir::Up;
    }
    return SwipeDir::None;
}

/* A recognised direction with no action must not disarm the press: a downward
 * drag below the top band is a list scroll and has to reach the list intact. */
bool actionable(SwipeDir d)
{
    switch (d) {
        case SwipeDir::Right: return true;
        case SwipeDir::Left:  return true;
        case SwipeDir::Down:  return s_c.y0 <= s_cfg.topBand;
        default:              return false;
    }
}

void gesture_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    if (s_inner) s_inner(indev, data);

    const bool down = data->state == LV_INDEV_STATE_PRESSED;

    if (down && !s_c.down) {
        s_c = Contact{true, true, false, data->point.x, data->point.y, lv_tick_get()};
        return;
    }
    if (!down) {
        s_c = Contact{};
        return;
    }
    if (s_c.fired) return;   // lv_indev_wait_release() ignores the rest of the contact
    if (!s_c.armed) return;

    const SwipeDir d = classify(data->point.x - s_c.x0, data->point.y - s_c.y0,
                                lv_tick_elaps(s_c.t0));
    if (d == SwipeDir::None) {
        /* Past the time limit this contact can no longer become a swipe. */
        if (lv_tick_elaps(s_c.t0) > s_cfg.maxMs) s_c.armed = false;
        return;
    }
    if (!actionable(d)) { s_c.armed = false; return; }
    if ((d == SwipeDir::Right || d == SwipeDir::Left) && consumesHorizontal(s_pressed)) {
        s_c.armed = false;
        return;
    }

    s_c.fired = true;
    s_last    = d;
    s_count++;
    /* Not lv_indev_reset(): that drops the pressed object silently, and an
     * object holding state on the press never learns the press ended. Swiping
     * across the sampler's pad row left a note sounding for exactly that
     * reason. wait_release() sends LV_EVENT_PRESS_LOST to the pressed object
     * and then ignores the rest of the contact, so no click is delivered on
     * lift either. */
    lv_indev_wait_release(indev);
    lv_async_call(act, (void*)(uintptr_t)d);
}

/* The object under the press. Indev events carry it as the event parameter;
 * pointer.act_obj itself is not public. */
void pressed_cb(lv_event_t* e)
{
    s_pressed = static_cast<lv_obj_t*>(lv_event_get_param(e));
}

} // namespace

void touch_gesture_attach(lv_indev_t* indev, const SwipeConfig& cfg)
{
    if (!indev || s_inner) return;
    s_cfg   = cfg;
    s_indev = indev;
    s_inner = lv_indev_get_read_cb(indev);
    lv_indev_set_read_cb(indev, gesture_read_cb);
    lv_indev_add_event_cb(indev, pressed_cb, LV_EVENT_PRESSED, nullptr);
}

SwipeDir touch_gesture_last()  { return s_last; }
uint32_t touch_gesture_count() { return s_count; }

const char* touch_gesture_forward_result() { return s_fwdResult; }

const char* swipeDirName(SwipeDir d)
{
    switch (d) {
        case SwipeDir::Right: return "right";
        case SwipeDir::Left:  return "left";
        case SwipeDir::Down:  return "down";
        case SwipeDir::Up:    return "up";
        default:              return "none";
    }
}

void touch_gesture_set_recents(RecentsFn fn) { s_recent = fn; }

} // namespace crosspad_gui

#endif // USE_LVGL
