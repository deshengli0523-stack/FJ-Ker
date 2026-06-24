#include "dither.h"

#include <algorithm>
#include <vector>

namespace dither {
void setPackedPixel(uint8_t* dst, int dstWidth, int x, int y, bool ink) {
  if (!dst || dstWidth <= 0 || x < 0 || y < 0 || x >= dstWidth) {
    return;
  }
  const int stride = (dstWidth + 7) / 8;
  const size_t index = static_cast<size_t>(y) * stride + static_cast<size_t>(x / 8);
  const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
  if (ink) {
    dst[index] |= mask;
  } else {
    dst[index] &= static_cast<uint8_t>(~mask);
  }
}

void floydSteinbergTo1bpp(const uint8_t* gray, int srcWidth, int srcHeight,
                          uint8_t* dst, int dstWidth, int dstHeight,
                          int dstX, int dstY) {
  if (!gray || !dst || srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) {
    return;
  }

  std::vector<int> work(static_cast<size_t>(srcWidth) * srcHeight);
  for (int i = 0; i < srcWidth * srcHeight; ++i) {
    work[static_cast<size_t>(i)] = gray[i];
  }

  for (int y = 0; y < srcHeight; ++y) {
    for (int x = 0; x < srcWidth; ++x) {
      const size_t idx = static_cast<size_t>(y) * srcWidth + x;
      const int oldValue = std::clamp(work[idx], 0, 255);
      const bool ink = oldValue <= 128;
      const int newValue = ink ? 0 : 255;
      const int error = oldValue - newValue;

      const int outX = dstX + x;
      const int outY = dstY + y;
      if (outX >= 0 && outX < dstWidth && outY >= 0 && outY < dstHeight) {
        setPackedPixel(dst, dstWidth, outX, outY, ink);
      }

      auto addError = [&](int nx, int ny, int numerator) {
        if (nx >= 0 && nx < srcWidth && ny >= 0 && ny < srcHeight) {
          const size_t nidx = static_cast<size_t>(ny) * srcWidth + nx;
          work[nidx] += (error * numerator) / 16;
        }
      };
      addError(x + 1, y, 7);
      addError(x - 1, y + 1, 3);
      addError(x, y + 1, 5);
      addError(x + 1, y + 1, 1);
    }
  }
}
}  // 命名空间 dither
