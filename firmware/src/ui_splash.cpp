#include "ui.h"

#include "display.h"

namespace ui {
void splash() {
  display::clear();
  display::drawText(126, 48, "FJ-ker", display::Font::Large);
  display::drawText(118, 94, "拍题答题阅读器", display::Font::Small);
  display::present();
}

void idle() {
  display::clear();
  display::drawText(126, 52, "按拍摄键", display::Font::Large);
  display::drawText(118, 96, "上传时连接 Wi-Fi", display::Font::Small);
  display::present();
}
}  // 命名空间 ui
