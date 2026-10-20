// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

/// Writes an RGB888 image as an uncompressed PNG (zlib stored blocks). False on I/O failure.
bool png_write_rgb(const char* path, const uint8_t* rgb, uint32_t w, uint32_t h);
