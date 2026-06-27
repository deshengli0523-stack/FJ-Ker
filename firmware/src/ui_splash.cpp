#include "ui.h"

#include "display.h"

namespace ui {
void splash() {
  display::clear();
  display::drawText(126, 48, "FJ-KER", display::Font::Large);
  display::drawText(108, 94, "CAMERA READER", display::Font::Small);
  display::present();
}

void idle() {
  display::clear();
  display::drawText(104, 52, "PRESS CAPTURE", display::Font::Large);
  display::drawText(96, 96, "CONFIRM AFTER PREVIEW", display::Font::Small);
  display::present();
}
}  // namespace ui
