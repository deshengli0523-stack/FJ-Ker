#include "ui.h"

#include <cstdio>

#include "display.h"

namespace ui {
void error(const char* message) {
  display::clear();
  display::drawText(20, 42, "错误", display::Font::Large);
  display::drawText(20, 86, message, display::Font::Small);
  display::drawText(20, 142, "取消: 预览", display::Font::Small);
  display::present();
}

void lowBattery(float volts) {
  char msg[32];
  std::snprintf(msg, sizeof(msg), "VBAT %.2fV", volts);
  display::clear();
  display::drawText(20, 52, "电量低", display::Font::Large);
  display::drawText(20, 96, msg, display::Font::Small);
  display::present();
}
}  // 命名空间 ui
