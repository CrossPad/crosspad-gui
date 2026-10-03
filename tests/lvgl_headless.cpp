#include "lvgl_headless.h"
#include <cstdint>
#include <vector>

#include "png_write.h"

namespace {
constexpr int32_t kHor = 320;
constexpr int32_t kVer = 240;
constexpr int32_t kBufLines = 24;
constexpr size_t kBytesPerPixel = 2;
alignas(4) uint8_t s_buf[kHor * kBufLines * kBytesPerPixel];
uint16_t s_frame[kHor * kVer];

void flush_cb(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
    const auto* src = reinterpret_cast<const uint16_t*>(px);
    const int32_t w = lv_area_get_width(area);
    for (int32_t y = area->y1; y <= area->y2; ++y)
        for (int32_t x = area->x1; x <= area->x2; ++x)
            s_frame[y * kHor + x] = src[(y - area->y1) * w + (x - area->x1)];
    lv_display_flush_ready(d);
}
}  // namespace

void lvgl_headless_init() {
    static bool done = false;
    if (done) return;
    done = true;
    lv_init();
    lv_display_t* d = lv_display_create(kHor, kVer);
    lv_display_set_buffers(d, s_buf, nullptr, sizeof s_buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
}

bool lvgl_headless_write_png(const char* path) {
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(nullptr);
    std::vector<uint8_t> rgb(static_cast<size_t>(kHor) * kVer * 3);
    for (int32_t i = 0; i < kHor * kVer; ++i) {
        const uint16_t p = s_frame[i];   // RGB565, expanded with the top bits replicated
        const uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
        rgb[i * 3] = static_cast<uint8_t>((r << 3) | (r >> 2));
        rgb[i * 3 + 1] = static_cast<uint8_t>((g << 2) | (g >> 4));
        rgb[i * 3 + 2] = static_cast<uint8_t>((b << 3) | (b >> 2));
    }
    return png_write_rgb(path, rgb.data(), kHor, kVer);
}
