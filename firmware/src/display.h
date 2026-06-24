#pragma once

#include <cstdint>

namespace display {
constexpr int PAGE_W = 384;
constexpr int PAGE_H = 168;
constexpr int PAGE_BYTES = 8064;

enum class Font : uint8_t { Small, Large };

void init();
void clear();
void blitPage(const uint8_t* page8064);
void blitPreviewWindow(const uint8_t* packed, int x, int y, int w, int h);
void drawText(int x, int y, const char* utf8, Font font);
void present();
void presentWindow(int x, int y, int w, int h);
}  // 命名空间 display
