#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "dither.h"

static int failures = 0;

static void expect_true(bool value, const char* message) {
  if (!value) {
    std::cerr << "失败: " << message << "\n";
    ++failures;
  }
}

static void expect_eq_u8(uint8_t actual, uint8_t expected, const char* message) {
  if (actual != expected) {
    std::cerr << "失败: " << message << " 期望 0x" << std::hex
              << static_cast<int>(expected) << " 实际 0x"
              << static_cast<int>(actual) << std::dec << "\n";
    ++failures;
  }
}

static void test_pack_pixel_msb_first() {
  uint8_t page[48 * 168];
  std::memset(page, 0, sizeof(page));

  dither::setPackedPixel(page, 384, 0, 0, true);
  expect_eq_u8(page[0], 0x80, "像素 (0,0) 设置字节 0 的 MSB");

  dither::setPackedPixel(page, 384, 7, 0, true);
  expect_eq_u8(page[0], 0x81, "像素 (7,0) 设置字节 0 的 LSB");

  dither::setPackedPixel(page, 384, 8, 0, true);
  expect_eq_u8(page[1], 0x80, "像素 (8,0) 设置字节 1 的 MSB");
}

static void test_threshold_and_offsets() {
  const uint8_t gray[4] = {0, 255, 0, 255};
  uint8_t page[48 * 168];
  std::memset(page, 0, sizeof(page));

  dither::floydSteinbergTo1bpp(gray, 2, 2, page, 384, 168, 1, 1);

  const int row1 = 1 * 48;
  const int row2 = 2 * 48;
  expect_eq_u8(page[row1], 0x40, "x=1 偏移将第一行墨点打包到 bit 6");
  expect_eq_u8(page[row2], 0x40, "y=1 偏移将第二行墨点打包到 bit 6");
}

static void test_clips_destination_bounds() {
  const uint8_t gray[4] = {0, 0, 0, 0};
  uint8_t page[48 * 168];
  std::memset(page, 0, sizeof(page));

  dither::floydSteinbergTo1bpp(gray, 2, 2, page, 384, 168, 383, 167);

  expect_eq_u8(page[167 * 48 + 47], 0x01, "只设置边界内的右下角像素");
}

int main() {
  test_pack_pixel_msb_first();
  test_threshold_and_offsets();
  test_clips_destination_bounds();
  if (failures != 0) {
    std::cerr << failures << " 个抖动测试失败\n";
    return 1;
  }
  std::cout << "抖动测试通过\n";
  return 0;
}
