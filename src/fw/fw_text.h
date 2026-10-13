// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cctype>
#include <cstddef>

namespace crosspad_gui::fw_text {

// strlcpy() without the BSD/glibc dependency: copies at most n-1 bytes and
// always terminates when n > 0.
inline void copy(char* dst, const char* src, size_t n) {
    if (n == 0) return;
    size_t i = 0;
    if (src) {
        for (; i + 1 < n && src[i]; ++i) dst[i] = src[i];
    }
    dst[i] = '\0';
}

// strcasecmp() for ASCII names (FATFS package folders), without <strings.h>.
inline int casecmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const int ca = std::tolower(static_cast<unsigned char>(*a));
        const int cb = std::tolower(static_cast<unsigned char>(*b));
        if (ca != cb || ca == 0) return ca - cb;
    }
}

}  // namespace crosspad_gui::fw_text
