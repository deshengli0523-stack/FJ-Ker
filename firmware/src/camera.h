#pragma once

#include <cstddef>
#include <cstdint>

namespace camera {
struct Frame {
  uint8_t* data = nullptr;
  size_t len = 0;
  int width = 0;
  int height = 0;
  void* opaque = nullptr;
  bool ownsData = false;
};

bool initPreview();
bool grabPreview(Frame& frame);
bool captureRaw(Frame& frame);
void release(Frame& frame);
}  // 命名空间 camera
