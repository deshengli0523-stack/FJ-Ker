#include "ui.h"

#include <cstdio>

#include "display.h"

namespace ui {
void answer(const uint8_t* page, int pageIndex, int pageCount) {
  if (page) {
    display::blitPage(page);
  } else {
    display::clear();
    display::drawText(138, 76, "LOADING PAGE", display::Font::Small);
  }
  char footer[32];
  std::snprintf(footer, sizeof(footer), "%d/%d", pageIndex + 1, pageCount);
  display::drawText(342, 154, footer, display::Font::Small);
  display::present();
}
}  // namespace ui
