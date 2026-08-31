#ifdef USE_LVGL

#include "crosspad-gui/components/app_icon.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include <cstdint>
#include <cstdio>

namespace crosspad_gui {

namespace {
// Sized for every app a launcher registers across a boot (IDF and PC apps,
// plus Power OFF); PC's assetPathPrefix() can be a resolved absolute
// filesystem path, hence the generous per-entry width.
constexpr size_t kMaxIconBufs = 48;
constexpr size_t kIconBufLen = 200;
char s_iconBufs[kMaxIconBufs][kIconBufLen];
size_t s_nextBuf = 0;
} // namespace

const char* resolveAppIcon(const char* icon) {
    if (!icon || icon[0] == '\0') return icon;
    // Already resolved (absolute path), or an LVGL symbol (LV_SYMBOL_* is a
    // 3-byte UTF-8 sequence starting at 0xEF, i.e. high bit set).
    if (icon[0] == '/' || (uint8_t)icon[0] >= 0x80) return icon;

    size_t idx = s_nextBuf < kMaxIconBufs ? s_nextBuf++ : kMaxIconBufs - 1;
    char* buf = s_iconBufs[idx];
    snprintf(buf, kIconBufLen, "%s%s", getGuiPlatform().assetPathPrefix(), icon);
    return buf;
}

} // namespace crosspad_gui

#endif // USE_LVGL
