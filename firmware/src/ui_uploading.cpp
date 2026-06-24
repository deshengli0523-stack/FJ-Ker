#include "ui.h"

#include "display.h"

namespace ui {
void uploading(const char* label, int spinnerFrame) {
  static const char* frames[] = {"|", "/", "-", "\\"};
  display::clear();
  display::drawText(132, 54, label, display::Font::Large);
  display::drawText(186, 96, frames[spinnerFrame & 3], display::Font::Large);
  display::present();
}
}  // 命名空间 ui
