#pragma once

#include <cstddef>
#include <cstdint>

namespace dither {
void setPackedPixel(uint8_t* dst, int dstWidth, int x, int y, bool ink);
void floydSteinbergTo1bpp(const uint8_t* gray, int srcWidth, int srcHeight,
                          uint8_t* dst, int dstWidth, int dstHeight,
                          int dstX, int dstY);
}  // 命名空间 dither
