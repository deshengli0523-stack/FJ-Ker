#include "ui.h"

#include <cstring>

#include "camera.h"
#include "display.h"
#include "dither.h"

namespace ui {
void preview() {
  static uint8_t page[display::PAGE_BYTES];
  std::memset(page, 0, sizeof(page));
  camera::Frame gray;
  if (camera::grabPreview(gray)) {
    dither::thresholdTo1bpp(gray.data, gray.width, gray.height, page,
                            display::PAGE_W, display::PAGE_H, 8, 20, 128);
    camera::release(gray);
  }
  display::blitPage(page);
  display::drawText(210, 72, "CONFIRM", display::Font::Small);
  display::presentWindow(0, 0, display::PAGE_W, display::PAGE_H);
}
}  // namespace ui
