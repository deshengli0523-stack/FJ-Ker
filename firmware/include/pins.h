#pragma once

#include <cstdint>

namespace pins {
constexpr int DISPLAY_SCLK = 47;
constexpr int DISPLAY_MOSI = 21;
constexpr int DISPLAY_CS = 42;
constexpr int DISPLAY_DC = 41;
constexpr int DISPLAY_RST = 40;

constexpr int BTN_CAPTURE = 45;
constexpr int BTN_CONFIRM = 48;
constexpr int BTN_CANCEL = 39;
constexpr int BTN_PAGE_UP = 38;
constexpr int BTN_PAGE_DOWN = 2;

constexpr int VBAT_SENSE = 1;

// 刷写前必须根据板载丝印核对 GOOUUU ESP32-S3-CAM 引脚定义。
// 这些默认值参考常见 ESP32-S3 OV2640 示例。
constexpr int CAM_PWDN = -1;
constexpr int CAM_RESET = -1;
constexpr int CAM_XCLK = 15;
constexpr int CAM_SIOD = 4;
constexpr int CAM_SIOC = 5;
constexpr int CAM_D7 = 16;
constexpr int CAM_D6 = 17;
constexpr int CAM_D5 = 18;
constexpr int CAM_D4 = 12;
constexpr int CAM_D3 = 10;
constexpr int CAM_D2 = 8;
constexpr int CAM_D1 = 9;
constexpr int CAM_D0 = 11;
constexpr int CAM_VSYNC = 6;
constexpr int CAM_HREF = 7;
constexpr int CAM_PCLK = 13;
}  // 命名空间 pins
