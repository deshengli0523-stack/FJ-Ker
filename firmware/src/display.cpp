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
uint8_t g_panelPage[PAGE_BYTES];

#ifndef FJKER_DISPLAY_SPI_HZ
#define FJKER_DISPLAY_SPI_HZ (20 * 1000 * 1000)
#endif

#ifndef FJKER_DISPLAY_SPI_MODE
#define FJKER_DISPLAY_SPI_MODE 0
#endif

constexpr uint8_t ST7305_COL_START = 0x17;
constexpr uint8_t ST7305_COL_END = 0x24;
constexpr uint8_t ST7305_ROW_START = 0x00;
constexpr uint8_t ST7305_ROW_END = 0xBF;
constexpr int ST7305_VERTICAL_PAGES = PAGE_H / 8;

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

void sendCommandData(uint8_t cmd, const uint8_t* data, size_t len, uint32_t delayMs = 0) {
  sendCommand(cmd);
  sendData(data, len);
  if (delayMs > 0) {
    delay(delayMs);
  }
}

void sendCommandByte(uint8_t cmd, uint8_t value, uint32_t delayMs = 0) {
  sendCommandData(cmd, &value, 1, delayMs);
}

void resetPanel() {
  digitalWrite(pins::DISPLAY_RST, HIGH);
  delay(10);
  digitalWrite(pins::DISPLAY_RST, LOW);
  delay(50);
  digitalWrite(pins::DISPLAY_RST, HIGH);
  delay(200);
}

void setFullWindow() {
  const uint8_t cols[] = {ST7305_COL_START, ST7305_COL_END};
  const uint8_t rows[] = {ST7305_ROW_START, ST7305_ROW_END};
  sendCommand(0x2A);
  sendData(cols, sizeof(cols));
  sendCommand(0x2B);
  sendData(rows, sizeof(rows));
  sendCommand(0x2C);
}

bool pagePixel(int x, int y) {
  constexpr int stride = (PAGE_W + 7) / 8;
  return (g_page[y * stride + x / 8] & (0x80u >> (x & 7))) != 0;
}

uint8_t verticalByte(int x, int page) {
  uint8_t value = 0;
  for (int bit = 0; bit < 8; ++bit) {
    const int y = page * 8 + bit;
    if (y < PAGE_H && pagePixel(x, y)) {
      value |= static_cast<uint8_t>(1u << bit);
    }
  }
  return value;
}

uint8_t packPairNibble(uint8_t b1, uint8_t b2) {
  return static_cast<uint8_t>(((b1 & 0x01) << 7) | ((b2 & 0x01) << 6) |
                              ((b1 & 0x02) << 4) | ((b2 & 0x02) << 3) |
                              ((b1 & 0x04) << 1) | (b2 & 0x04) |
                              ((b1 & 0x08) >> 2) | ((b2 & 0x08) >> 3));
}

void packPanelPage() {
  constexpr int stride = (PAGE_W + 7) / 8;
  size_t out = 0;
  for (int x = 0; x < PAGE_W; x += 2) {
    const int byteIndex = x / 8;
    const uint8_t mask1 = static_cast<uint8_t>(0x80u >> (x & 7));
    const uint8_t mask2 = static_cast<uint8_t>(0x80u >> ((x + 1) & 7));
    for (int page = 0; page < ST7305_VERTICAL_PAGES; page += 3) {
      for (int pageOffset = 0; pageOffset < 3; ++pageOffset) {
        uint8_t b1 = 0;
        uint8_t b2 = 0;
        const int baseY = (page + pageOffset) * 8;
        for (int bit = 0; bit < 8; ++bit) {
          const uint8_t rowByte = g_page[(baseY + bit) * stride + byteIndex];
          if ((rowByte & mask1) != 0) {
            b1 |= static_cast<uint8_t>(1u << bit);
          }
          if ((rowByte & mask2) != 0) {
            b2 |= static_cast<uint8_t>(1u << bit);
          }
        }
        g_panelPage[out++] = packPairNibble(b1, b2);
        g_panelPage[out++] = packPairNibble(static_cast<uint8_t>(b1 >> 4),
                                            static_cast<uint8_t>(b2 >> 4));
      }
    }
  }
}

void initSt7305() {
  const uint8_t b3[] = {0xE5, 0xF6, 0x17, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x71};
  const uint8_t b4[] = {0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45};
  const uint8_t gt[] = {0x32, 0x03, 0x1F};

  sendCommandData(0xD6, reinterpret_cast<const uint8_t*>("\x13\x02"), 2);
  sendCommandByte(0xD1, 0x01);
  sendCommandData(0xC0, reinterpret_cast<const uint8_t*>("\x08\x06"), 2);
  sendCommandData(0xC1, reinterpret_cast<const uint8_t*>("\x3C\x3E\x3C\x3C"), 4);
  sendCommandData(0xC2, reinterpret_cast<const uint8_t*>("\x23\x21\x23\x23"), 4);
  sendCommandData(0xC4, reinterpret_cast<const uint8_t*>("\x5A\x5C\x5A\x5A"), 4);
  sendCommandData(0xC5, reinterpret_cast<const uint8_t*>("\x37\x35\x37\x37"), 4);
  sendCommandByte(0xB2, 0x05);
  sendCommandData(0xB3, b3, sizeof(b3));
  sendCommandData(0xB4, b4, sizeof(b4));
  sendCommandData(0x62, gt, sizeof(gt));
  sendCommandByte(0xB7, 0x13);
  sendCommandByte(0xB0, 0x60);
  sendCommand(0x11);
  delay(100);
  sendCommandByte(0xC9, 0x00);
  sendCommandByte(0x36, 0x00);
  sendCommandByte(0x3A, 0x11);
  sendCommandByte(0xB9, 0x20);
  sendCommandByte(0xB8, 0x29);
  const uint8_t cols[] = {ST7305_COL_START, ST7305_COL_END};
  const uint8_t rows[] = {ST7305_ROW_START, ST7305_ROW_END};
  sendCommandData(0x2A, cols, sizeof(cols));
  sendCommandData(0x2B, rows, sizeof(rows));
  sendCommandByte(0xD0, 0xFF);
  sendCommand(0x39);
  sendCommand(0x20);
  sendCommand(0x29);
  delay(100);
}
#endif

void setPagePixel(int x, int y) {
  if (x >= 0 && x < PAGE_W && y >= 0 && y < PAGE_H) {
    const int idx = y * ((PAGE_W + 7) / 8) + x / 8;
    g_page[idx] |= static_cast<uint8_t>(0x80u >> (x & 7));
  }
}

const char* glyphRows(char c) {
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }
  switch (c) {
    case ' ': return "00000""00000""00000""00000""00000""00000""00000";
    case '0': return "01110""10001""10011""10101""11001""10001""01110";
    case '1': return "00100""01100""00100""00100""00100""00100""01110";
    case '2': return "01110""10001""00001""00010""00100""01000""11111";
    case '3': return "11110""00001""00001""01110""00001""00001""11110";
    case '4': return "00010""00110""01010""10010""11111""00010""00010";
    case '5': return "11111""10000""10000""11110""00001""00001""11110";
    case '6': return "01110""10000""10000""11110""10001""10001""01110";
    case '7': return "11111""00001""00010""00100""01000""01000""01000";
    case '8': return "01110""10001""10001""01110""10001""10001""01110";
    case '9': return "01110""10001""10001""01111""00001""00001""01110";
    case 'A': return "01110""10001""10001""11111""10001""10001""10001";
    case 'B': return "11110""10001""10001""11110""10001""10001""11110";
    case 'C': return "01111""10000""10000""10000""10000""10000""01111";
    case 'D': return "11110""10001""10001""10001""10001""10001""11110";
    case 'E': return "11111""10000""10000""11110""10000""10000""11111";
    case 'F': return "11111""10000""10000""11110""10000""10000""10000";
    case 'G': return "01111""10000""10000""10111""10001""10001""01111";
    case 'H': return "10001""10001""10001""11111""10001""10001""10001";
    case 'I': return "01110""00100""00100""00100""00100""00100""01110";
    case 'J': return "00111""00010""00010""00010""00010""10010""01100";
    case 'K': return "10001""10010""10100""11000""10100""10010""10001";
    case 'L': return "10000""10000""10000""10000""10000""10000""11111";
    case 'M': return "10001""11011""10101""10101""10001""10001""10001";
    case 'N': return "10001""11001""10101""10011""10001""10001""10001";
    case 'O': return "01110""10001""10001""10001""10001""10001""01110";
    case 'P': return "11110""10001""10001""11110""10000""10000""10000";
    case 'Q': return "01110""10001""10001""10001""10101""10010""01101";
    case 'R': return "11110""10001""10001""11110""10100""10010""10001";
    case 'S': return "01111""10000""10000""01110""00001""00001""11110";
    case 'T': return "11111""00100""00100""00100""00100""00100""00100";
    case 'U': return "10001""10001""10001""10001""10001""10001""01110";
    case 'V': return "10001""10001""10001""10001""10001""01010""00100";
    case 'W': return "10001""10001""10001""10101""10101""10101""01010";
    case 'X': return "10001""10001""01010""00100""01010""10001""10001";
    case 'Y': return "10001""10001""01010""00100""00100""00100""00100";
    case 'Z': return "11111""00001""00010""00100""01000""10000""11111";
    case '-': return "00000""00000""00000""11111""00000""00000""00000";
    case '_': return "00000""00000""00000""00000""00000""00000""11111";
    case ':': return "00000""00100""00100""00000""00100""00100""00000";
    case '.': return "00000""00000""00000""00000""00000""01100""01100";
    case '/': return "00001""00010""00010""00100""01000""01000""10000";
    case '\\': return "10000""01000""01000""00100""00010""00010""00001";
    case '|': return "00100""00100""00100""00100""00100""00100""00100";
    case '?': return "01110""10001""00001""00010""00100""00000""00100";
    case '!': return "00100""00100""00100""00100""00100""00000""00100";
    case '%': return "11001""11010""00010""00100""01000""01011""10011";
    default: return "01110""10001""00001""00010""00100""00000""00100";
  }
}

void drawBlockChar(int x, int y, char c, Font font) {
  const int scale = font == Font::Large ? 2 : 1;
  const char* rows = glyphRows(c);
  for (int gy = 0; gy < 7; ++gy) {
    for (int gx = 0; gx < 5; ++gx) {
      if (rows[gy * 5 + gx] == '1') {
        for (int sy = 0; sy < scale; ++sy) {
          for (int sx = 0; sx < scale; ++sx) {
            setPagePixel(x + gx * scale + sx, y + gy * scale + sy);
          }
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
  dev.clock_speed_hz = FJKER_DISPLAY_SPI_HZ;
  dev.mode = FJKER_DISPLAY_SPI_MODE;
  dev.spics_io_num = pins::DISPLAY_CS;
  dev.queue_size = 1;
  spi_bus_add_device(SPI3_HOST, &dev, &g_spi);

  resetPanel();
  initSt7305();
#if 0
  sendCommand(0x01);
  delay(150);
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
  delay(50);
#endif
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
  packPanelPage();
  sendData(g_panelPage, sizeof(g_panelPage));
#endif
}

void presentWindow(int x, int y, int w, int h) {
#ifdef ARDUINO
  present();
#if 0
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
  sendCommand(0x12);
  // ST7305 局部刷新命令；需在硬件上确认厂商的精确序列。
  sendCommand(0x12);
#endif
#else
  (void)x;
  (void)y;
  (void)w;
  (void)h;
#endif
}
}  // 命名空间 display
