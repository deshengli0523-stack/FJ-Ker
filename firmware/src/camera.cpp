#include "camera.h"

#ifdef ARDUINO
#include "esp_camera.h"
#include "pins.h"
#endif

namespace camera {
#ifdef ARDUINO
namespace {
bool g_initialized = false;
framesize_t g_currentSize = FRAMESIZE_QQVGA;
pixformat_t g_currentFormat = PIXFORMAT_GRAYSCALE;

bool configure(framesize_t size, pixformat_t format, int jpegQuality) {
  if (g_initialized && g_currentSize == size && g_currentFormat == format) {
    return true;
  }
  if (g_initialized) {
    esp_camera_deinit();
    g_initialized = false;
  }

  camera_config_t cfg = {};
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.pin_d0 = pins::CAM_D0;
  cfg.pin_d1 = pins::CAM_D1;
  cfg.pin_d2 = pins::CAM_D2;
  cfg.pin_d3 = pins::CAM_D3;
  cfg.pin_d4 = pins::CAM_D4;
  cfg.pin_d5 = pins::CAM_D5;
  cfg.pin_d6 = pins::CAM_D6;
  cfg.pin_d7 = pins::CAM_D7;
  cfg.pin_xclk = pins::CAM_XCLK;
  cfg.pin_pclk = pins::CAM_PCLK;
  cfg.pin_vsync = pins::CAM_VSYNC;
  cfg.pin_href = pins::CAM_HREF;
  cfg.pin_sccb_sda = pins::CAM_SIOD;
  cfg.pin_sccb_scl = pins::CAM_SIOC;
  cfg.pin_pwdn = pins::CAM_PWDN;
  cfg.pin_reset = pins::CAM_RESET;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = format;
  cfg.frame_size = size;
  cfg.jpeg_quality = jpegQuality;
  cfg.fb_count = 2;
  cfg.fb_location = CAMERA_FB_IN_PSRAM;
  cfg.grab_mode = CAMERA_GRAB_LATEST;

  if (esp_camera_init(&cfg) != ESP_OK) {
    return false;
  }
  g_initialized = true;
  g_currentSize = size;
  g_currentFormat = format;
  return true;
}
}  // 命名空间
#endif

bool initPreview() {
#ifdef ARDUINO
  return configure(FRAMESIZE_QQVGA, PIXFORMAT_GRAYSCALE, 12);
#else
  return false;
#endif
}

bool grabPreview(Frame& frame) {
#ifdef ARDUINO
  if (!configure(FRAMESIZE_QQVGA, PIXFORMAT_GRAYSCALE, 12)) {
    return false;
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    return false;
  }
  frame.data = fb->buf;
  frame.len = fb->len;
  frame.width = fb->width;
  frame.height = fb->height;
  frame.opaque = fb;
  return true;
#else
  (void)frame;
  return false;
#endif
}

bool captureJpeg(Frame& frame) {
#ifdef ARDUINO
  if (!configure(FRAMESIZE_UXGA, PIXFORMAT_JPEG, 12)) {
    return false;
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    return false;
  }
  frame.data = fb->buf;
  frame.len = fb->len;
  frame.width = fb->width;
  frame.height = fb->height;
  frame.opaque = fb;
  return true;
#else
  (void)frame;
  return false;
#endif
}

void release(Frame& frame) {
#ifdef ARDUINO
  if (frame.opaque) {
    esp_camera_fb_return(static_cast<camera_fb_t*>(frame.opaque));
  }
#endif
  frame = Frame{};
}
}  // 命名空间 camera
