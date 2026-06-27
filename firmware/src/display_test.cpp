#include "display_test.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <cstring>

#include "display.h"

#ifdef ARDUINO
#include "pins.h"
#endif

namespace display_test {
namespace {
constexpr unsigned long FRAME_MS = 1200;
uint8_t g_page[display::PAGE_BYTES];
unsigned long g_lastFrameAt = 0;
int g_frame = -1;

void setPixel(int x, int y, bool on) {
  if (x < 0 || x >= display::PAGE_W || y < 0 || y >= display::PAGE_H) {
    return;
  }
  constexpr int stride = (display::PAGE_W + 7) / 8;
  const int index = y * stride + x / 8;
  const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
  if (on) {
    g_page[index] |= mask;
  } else {
    g_page[index] &= static_cast<uint8_t>(~mask);
  }
}

void clearTo(uint8_t value) { std::memset(g_page, value, sizeof(g_page)); }

void verticalStripes() {
  clearTo(0x00);
  for (int y = 0; y < display::PAGE_H; ++y) {
    for (int x = 0; x < display::PAGE_W; ++x) {
      setPixel(x, y, ((x / 12) % 2) == 0);
    }
  }
}

void horizontalStripes() {
  clearTo(0x00);
  for (int y = 0; y < display::PAGE_H; ++y) {
    for (int x = 0; x < display::PAGE_W; ++x) {
      setPixel(x, y, ((y / 12) % 2) == 0);
    }
  }
}

void checkerboard() {
  clearTo(0x00);
  for (int y = 0; y < display::PAGE_H; ++y) {
    for (int x = 0; x < display::PAGE_W; ++x) {
      setPixel(x, y, (((x / 16) + (y / 16)) % 2) == 0);
    }
  }
}

void borderAndDiagonals() {
  clearTo(0x00);
  for (int x = 0; x < display::PAGE_W; ++x) {
    setPixel(x, 0, true);
    setPixel(x, display::PAGE_H - 1, true);
  }
  for (int y = 0; y < display::PAGE_H; ++y) {
    setPixel(0, y, true);
    setPixel(display::PAGE_W - 1, y, true);
  }
  for (int x = 0; x < display::PAGE_W; ++x) {
    const int y1 = (x * display::PAGE_H) / display::PAGE_W;
    const int y2 = display::PAGE_H - 1 - y1;
    setPixel(x, y1, true);
    setPixel(x, y2, true);
  }
}

const char* renderFrame(int frame) {
  switch (frame) {
    case 0:
      clearTo(0x00);
      return "blank";
    case 1:
      clearTo(0xFF);
      return "filled";
    case 2:
      verticalStripes();
      return "vertical";
    case 3:
      horizontalStripes();
      return "horizontal";
    case 4:
      checkerboard();
      return "checker";
    default:
      borderAndDiagonals();
      return "border";
  }
}

void showFrame(int frame) {
  const char* name = renderFrame(frame);
  display::blitPage(g_page);
  display::drawText(12, 12, "DISPLAY TEST", display::Font::Large);
  display::drawText(12, 42, name, display::Font::Small);
  display::presentWindow(0, 0, display::PAGE_W, display::PAGE_H);
#ifdef ARDUINO
  Serial.printf("[FJKER][display_test] frame=%d name=%s\n", frame, name);
#endif
}
}  // namespace

void setup() {
#ifdef ARDUINO
  Serial.println("[FJKER][display_test] boot");
  Serial.printf("[FJKER][display_test] pins sclk=%d mosi=%d cs=%d dc=%d rst=%d\n",
                pins::DISPLAY_SCLK, pins::DISPLAY_MOSI, pins::DISPLAY_CS,
                pins::DISPLAY_DC, pins::DISPLAY_RST);
#endif
  display::init();
  g_lastFrameAt = millis();
  g_frame = 0;
  showFrame(g_frame);
}

void tick() {
#ifdef ARDUINO
  const unsigned long now = millis();
  if (now - g_lastFrameAt < FRAME_MS) {
    return;
  }
  g_lastFrameAt = now;
  g_frame = (g_frame + 1) % 6;
  showFrame(g_frame);
#endif
}
}  // namespace display_test
