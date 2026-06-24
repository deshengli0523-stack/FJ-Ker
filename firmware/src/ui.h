#pragma once

#include <cstdint>

namespace ui {
void splash();
void idle();
void preview();
void uploading(const char* label, int spinnerFrame);
void answer(const uint8_t* page, int pageIndex, int pageCount);
void error(const char* message);
void lowBattery(float volts);
}  // 命名空间 ui
