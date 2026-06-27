#include "ui.h"

#include <cstdio>

#include "display.h"

namespace ui {
void error(const char* message) {
  display::clear();
  display::drawText(20, 42, "ERROR", display::Font::Large);
  display::drawText(20, 86, message, display::Font::Small);
  display::drawText(20, 142, "CANCEL: PREVIEW", display::Font::Small);
  display::present();
}

void lowBattery(float volts) {
  char msg[32];
  std::snprintf(msg, sizeof(msg), "VBAT %.2fV", volts);
  display::clear();
  display::drawText(20, 52, "LOW BAT", display::Font::Large);
  display::drawText(20, 96, msg, display::Font::Small);
  display::present();
}
}  // namespace ui
