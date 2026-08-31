#pragma once

#ifdef USE_LVGL

/**
 * @file app_icon.h
 * @brief Portable app icon path resolution.
 *
 * Every REGISTER_APP call site used to snprintf its own
 * "<assetPathPrefix><icon>" path into a static buffer before building its
 * AppEntry — duplicated verbatim across platforms and apps. resolveAppIcon()
 * is that one line, shared: it asks the platform for its asset prefix
 * (IGuiPlatform::assetPathPrefix(), e.g. "/assets/" on IDF, "assets/" on PC)
 * and leaves already-resolved icons (absolute paths, LVGL symbols) alone.
 */

namespace crosspad_gui {

/// Resolve an app icon filename (e.g. "settings.png") to a platform-specific
/// path by prepending IGuiPlatform::assetPathPrefix(). Returns the input
/// pointer unchanged if it is null/empty, already an absolute path
/// (starts with '/'), or an LVGL symbol (LV_SYMBOL_*, high-bit UTF-8).
///
/// The returned pointer is valid for the lifetime of the process — it comes
/// from an internal rotating pool of static buffers, sized generously for
/// the number of apps a launcher registers. Call once per app at
/// registration time (as REGISTER_APP call sites do) and keep the result,
/// rather than re-resolving on every use.
const char* resolveAppIcon(const char* icon);

} // namespace crosspad_gui

#endif // USE_LVGL
