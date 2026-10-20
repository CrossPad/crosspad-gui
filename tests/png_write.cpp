// SPDX-License-Identifier: GPL-3.0-or-later
#include "png_write.h"

#include <cstdio>
#include <vector>

namespace {

constexpr size_t kStoredMax = 65535;   // a stored deflate block holds at most this much

uint32_t crc32_update(uint32_t crc, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s));
}

bool chunk(FILE* f, const char type[4], const std::vector<uint8_t>& data) {
    std::vector<uint8_t> head;
    put32(head, static_cast<uint32_t>(data.size()));
    head.insert(head.end(), type, type + 4);
    uint32_t crc = crc32_update(0xFFFFFFFFu, head.data() + 4, 4);
    crc = ~crc32_update(crc, data.data(), data.size());
    std::vector<uint8_t> tail;
    put32(tail, crc);
    return fwrite(head.data(), 1, head.size(), f) == head.size() &&
           (data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size()) &&
           fwrite(tail.data(), 1, tail.size(), f) == tail.size();
}

}  // namespace

bool png_write_rgb(const char* path, const uint8_t* rgb, uint32_t w, uint32_t h) {
    std::vector<uint8_t> raw;   // each scanline led by filter type 0
    raw.reserve((static_cast<size_t>(w) * 3 + 1) * h);
    for (uint32_t y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb + static_cast<size_t>(y) * w * 3, rgb + static_cast<size_t>(y + 1) * w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521u;
        b = (b + a) % 65521u;
    }
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        const size_t n = raw.size() - pos < kStoredMax ? raw.size() - pos : kStoredMax;
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n));
        z.push_back(static_cast<uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
        if (last) break;
    }
    put32(z, (b << 16) | a);

    std::vector<uint8_t> ihdr;
    put32(ihdr, w);
    put32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});   // 8-bit RGB, no interlace

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const bool ok = fwrite(sig, 1, sizeof sig, f) == sizeof sig && chunk(f, "IHDR", ihdr) &&
                    chunk(f, "IDAT", z) && chunk(f, "IEND", {});
    return fclose(f) == 0 && ok;
}
