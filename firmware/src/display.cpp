#include "display.h"

#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include "driver/spi_master.h"
#include "pins.h"
#endif

namespace display {
namespace {
uint8_t g_page[PAGE_BYTES];

#ifdef ARDUINO
spi_device_handle_t g_spi = nullptr;

void sendCommand(uint8_t cmd) {
  digitalWrite(pins::DISPLAY_DC, LOW);
  spi_transaction_t tx = {};
  tx.length = 8;
  tx.tx_buffer = &cmd;
  spi_device_transmit(g_spi, &tx);
}

void sendData(const uint8_t* data, size_t len) {
  if (!data || len == 0) {
    return;
  }
  digitalWrite(pins::DISPLAY_DC, HIGH);
  spi_transaction_t tx = {};
  tx.length = len * 8;
  tx.tx_buffer = data;
  spi_device_transmit(g_spi, &tx);
}

void sendDataByte(uint8_t value) { sendData(&value, 1); }

void resetPanel() {
  digitalWrite(pins::DISPLAY_RST, LOW);
  delay(20);
  digitalWrite(pins::DISPLAY_RST, HIGH);
  delay(120);
}

void setWindow(int x, int y, int w, int h) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > PAGE_W) {
    w = PAGE_W - x;
  }
  if (y + h > PAGE_H) {
    h = PAGE_H - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }
  const int x2 = x + w - 1;
  const int y2 = y + h - 1;
  sendCommand(0x2A);
  const uint8_t cols[] = {static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x & 0xFF),
                          static_cast<uint8_t>(x2 >> 8), static_cast<uint8_t>(x2 & 0xFF)};
  sendData(cols, sizeof(cols));
  sendCommand(0x2B);
  const uint8_t rows[] = {static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y & 0xFF),
                          static_cast<uint8_t>(y2 >> 8), static_cast<uint8_t>(y2 & 0xFF)};
  sendData(rows, sizeof(rows));
  sendCommand(0x2C);
}

void setFullWindow() { setWindow(0, 0, PAGE_W, PAGE_H); }
#endif

void drawBlockChar(int x, int y, char c, Font font) {
  if (c == ' ') {
    return;
  }
  const int scale = font == Font::Large ? 2 : 1;
  const int w = 4 * scale;
  const int h = 7 * scale;
  for (int py = 0; py < h; ++py) {
    for (int px = 0; px < w; ++px) {
      const bool edge = px == 0 || px == w - 1 || py == 0 || py == h - 1;
      const bool fill = ((static_cast<unsigned char>(c) + px / scale + py / scale) % 5) == 0;
      if (edge || fill) {
        const int sx = x + px;
        const int sy = y + py;
        if (sx >= 0 && sx < PAGE_W && sy >= 0 && sy < PAGE_H) {
          const int idx = sy * ((PAGE_W + 7) / 8) + sx / 8;
          g_page[idx] |= static_cast<uint8_t>(0x80u >> (sx & 7));
        }
      }
    }
  }
}
}  // 命名空间

void init() {
#ifdef ARDUINO
  pinMode(pins::DISPLAY_DC, OUTPUT);
  pinMode(pins::DISPLAY_RST, OUTPUT);
  pinMode(pins::DISPLAY_CS, OUTPUT);
  digitalWrite(pins::DISPLAY_CS, HIGH);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = pins::DISPLAY_MOSI;
  bus.miso_io_num = -1;
  bus.sclk_io_num = pins::DISPLAY_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = PAGE_BYTES + 16;
  spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = 20 * 1000 * 1000;
  dev.mode = 0;
  dev.spics_io_num = pins::DISPLAY_CS;
  dev.queue_size = 1;
  spi_bus_add_device(SPI3_HOST, &dev, &g_spi);

  resetPanel();
  // TODO: 首次在 YDP290H001 硬件测试后，将这套保守的 ST7305 初始化序列
  // 替换为厂商的精确序列。
  sendCommand(0x01);
  delay(120);
  sendCommand(0x11);
  delay(120);
  sendCommand(0x36);
  sendDataByte(0x00);
  sendCommand(0x3A);
  sendDataByte(0x01);
  sendCommand(0x29);
#endif
  clear();
}

void clear() { std::memset(g_page, 0, sizeof(g_page)); }

void blitPage(const uint8_t* page8064) {
  if (page8064) {
    std::memcpy(g_page, page8064, PAGE_BYTES);
  }
}

void blitPreviewWindow(const uint8_t* packed, int x, int y, int w, int h) {
  if (!packed || w <= 0 || h <= 0) {
    return;
  }
  const int srcStride = (w + 7) / 8;
  const int dstStride = (PAGE_W + 7) / 8;
  for (int row = 0; row < h; ++row) {
    const int dstY = y + row;
    if (dstY < 0 || dstY >= PAGE_H) {
      continue;
    }
    for (int col = 0; col < w; ++col) {
      const int dstX = x + col;
      if (dstX < 0 || dstX >= PAGE_W) {
        continue;
      }
      const bool ink = (packed[row * srcStride + col / 8] & (0x80u >> (col & 7))) != 0;
      const int idx = dstY * dstStride + dstX / 8;
      const uint8_t mask = static_cast<uint8_t>(0x80u >> (dstX & 7));
      if (ink) {
        g_page[idx] |= mask;
      } else {
        g_page[idx] &= static_cast<uint8_t>(~mask);
      }
    }
  }
}

void drawText(int x, int y, const char* utf8, Font font) {
  if (!utf8) {
    return;
  }
  const int step = font == Font::Large ? 12 : 6;
  int cursor = x;
  for (const char* p = utf8; *p != '\0'; ++p) {
    drawBlockChar(cursor, y, *p, font);
    cursor += step;
  }
}

void present() {
#ifdef ARDUINO
  setFullWindow();
  sendData(g_page, sizeof(g_page));
#endif
}

void presentWindow(int x, int y, int w, int h) {
#ifdef ARDUINO
  if (x != 0 || w != PAGE_W || y < 0 || y >= PAGE_H || h <= 0) {
    present();
    return;
  }
  if (y + h > PAGE_H) {
    h = PAGE_H - y;
  }
  setWindow(0, y, PAGE_W, h);
  const int stride = (PAGE_W + 7) / 8;
  sendData(g_page + y * stride, static_cast<size_t>(h) * stride);
  // ST7305 局部刷新命令；需在硬件上确认厂商的精确序列。
  sendCommand(0x12);
#else
  (void)x;
  (void)y;
  (void)w;
  (void)h;
#endif
}
}  // 命名空间 display
