#pragma once

#include <cstdint>

namespace pins {
constexpr int DISPLAY_SCLK = 47;
constexpr int DISPLAY_MOSI = 21;
constexpr int DISPLAY_CS = 42;
constexpr int DISPLAY_DC = 41;
constexpr int DISPLAY_RST = 40;

constexpr int BTN_CAPTURE = 45;
constexpr int BTN_CONFIRM = 14;
constexpr int BTN_CANCEL = 39;
constexpr int BTN_PAGE_UP = 38;
constexpr int BTN_PAGE_DOWN = 2;

constexpr int VBAT_SENSE = 1;

// Camera pins for this board use the CAMERA_MODEL_ESP32S3_EYE mapping.
// Keep these GPIO values in sync with the ESP32-S3-EYE OV2640 example.
// CAM_D0..CAM_D7 map to camera data lines Y2..Y9 respectively.
// Do not use the CAMERA_MODEL_ESP32S3_CAM_LCD branch; that board has
// different camera GPIOs.
constexpr int CAM_PWDN = -1;
constexpr int CAM_RESET = -1;
constexpr int CAM_XCLK = 15;
constexpr int CAM_SIOD = 4;
constexpr int CAM_SIOC = 5;
constexpr int CAM_D7 = 16;  // Y9
constexpr int CAM_D6 = 17;  // Y8
constexpr int CAM_D5 = 18;  // Y7
constexpr int CAM_D4 = 12;  // Y6
constexpr int CAM_D3 = 10;  // Y5
constexpr int CAM_D2 = 8;   // Y4
constexpr int CAM_D1 = 9;   // Y3
constexpr int CAM_D0 = 11;  // Y2
constexpr int CAM_VSYNC = 6;
constexpr int CAM_HREF = 7;
constexpr int CAM_PCLK = 13;
}  // namespace pins
