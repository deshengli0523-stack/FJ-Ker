#include "camera.h"

#include "diagnostics.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <cstdlib>
#include "esp_camera.h"
#include "pins.h"
#endif

namespace camera {
#ifdef ARDUINO
namespace {
constexpr framesize_t CAMERA_SIZE = FRAMESIZE_SVGA;
constexpr pixformat_t CAMERA_FORMAT = PIXFORMAT_RGB565;
constexpr int PREVIEW_W = 160;
constexpr int PREVIEW_H = 120;

bool g_initialized = false;
uint8_t g_previewGray[PREVIEW_W * PREVIEW_H];
unsigned long g_lastPreviewFailureLogAt = 0;

framesize_t frameSizeForMemory(framesize_t requested) {
  if (psramFound() || requested <= FRAMESIZE_SVGA) {
    return requested;
  }
  return FRAMESIZE_SVGA;
}

void logPreviewFailure(const char* message) {
  const unsigned long now = millis();
  if (g_lastPreviewFailureLogAt == 0 || now - g_lastPreviewFailureLogAt >= 2000) {
    g_lastPreviewFailureLogAt = now;
    diagnostics::event("camera", message);
  }
}

bool initializeCamera() {
  if (g_initialized) {
    return true;
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
  cfg.frame_size = CAMERA_SIZE;
  cfg.pixel_format = CAMERA_FORMAT;
  cfg.jpeg_quality = 12;
  cfg.fb_count = 1;
  cfg.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  if (!psramFound()) {
    cfg.frame_size = frameSizeForMemory(CAMERA_SIZE);
  }

  diagnostics::value("camera", "psram", psramFound() ? 1 : 0);
  diagnostics::value("camera", "request_size", static_cast<long long>(CAMERA_SIZE));
  diagnostics::value("camera", "actual_size", static_cast<long long>(cfg.frame_size));
  diagnostics::value("camera", "format", static_cast<long long>(CAMERA_FORMAT));
  diagnostics::value("camera", "fb_count", cfg.fb_count);
  diagnostics::size("camera", "free_heap", ESP.getFreeHeap());
  diagnostics::size("camera", "free_psram", ESP.getFreePsram());

  const unsigned long start = millis();
  const esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    diagnostics::value("camera", "init_err", static_cast<long long>(err));
    return false;
  }
  diagnostics::value("camera", "init_ms", static_cast<long long>(millis() - start));

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, 0);
    diagnostics::event("camera", "sensor tuned");
  }

  g_initialized = true;
  diagnostics::event("camera", "init ok");
  return true;
}

uint8_t rgb565ToGray(uint8_t high, uint8_t low) {
  const uint16_t pixel = static_cast<uint16_t>((high << 8) | low);
  const uint8_t r = static_cast<uint8_t>(((pixel >> 11) & 0x1F) << 3);
  const uint8_t g = static_cast<uint8_t>(((pixel >> 5) & 0x3F) << 2);
  const uint8_t b = static_cast<uint8_t>((pixel & 0x1F) << 3);
  return static_cast<uint8_t>((static_cast<unsigned>(r) * 30u +
                               static_cast<unsigned>(g) * 59u +
                               static_cast<unsigned>(b) * 11u) /
                              100u);
}

bool downsamplePreview(const camera_fb_t* fb) {
  if (!fb || fb->format != CAMERA_FORMAT || fb->width == 0 || fb->height == 0) {
    return false;
  }
  const int srcStride = static_cast<int>(fb->width) * 2;
  for (int y = 0; y < PREVIEW_H; ++y) {
    const int srcY = static_cast<int>((static_cast<unsigned long>(y) * fb->height) / PREVIEW_H);
    const uint8_t* row = fb->buf + srcY * srcStride;
    for (int x = 0; x < PREVIEW_W; ++x) {
      const int srcX =
          static_cast<int>((static_cast<unsigned long>(x) * fb->width) / PREVIEW_W) * 2;
      g_previewGray[y * PREVIEW_W + x] = rgb565ToGray(row[srcX], row[srcX + 1]);
    }
  }
  return true;
}
}  // namespace
#endif

bool initPreview() {
#ifdef ARDUINO
  const bool ok = initializeCamera();
  diagnostics::result("camera", "init_preview", ok);
  return ok;
#else
  return false;
#endif
}

bool grabPreview(Frame& frame) {
#ifdef ARDUINO
  if (!initializeCamera()) {
    logPreviewFailure("preview configure failed");
    return false;
  }
  const unsigned long start = millis();
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    logPreviewFailure("preview framebuffer failed");
    return false;
  }
  const bool ok = downsamplePreview(fb);
  esp_camera_fb_return(fb);
  if (!ok) {
    logPreviewFailure("preview downsample failed");
    return false;
  }
  frame.data = g_previewGray;
  frame.len = sizeof(g_previewGray);
  frame.width = PREVIEW_W;
  frame.height = PREVIEW_H;
  frame.opaque = nullptr;
  frame.ownsData = false;
  static unsigned long lastLog = 0;
  if (lastLog == 0 || millis() - lastLog >= 3000) {
    lastLog = millis();
    diagnostics::value("camera", "preview_frame_ms", static_cast<long long>(millis() - start));
  }
  return true;
#else
  (void)frame;
  return false;
#endif
}

bool captureRaw(Frame& frame) {
#ifdef ARDUINO
  if (!initializeCamera()) {
    diagnostics::event("camera", "capture configure failed");
    return false;
  }

  const unsigned long start = millis();
  diagnostics::event("camera", "capture framebuffer start");
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    diagnostics::event("camera", "capture framebuffer failed");
    return false;
  }
  diagnostics::value("camera", "capture_fb_ms", static_cast<long long>(millis() - start));

  frame.data = fb->buf;
  frame.len = fb->len;
  frame.width = fb->width;
  frame.height = fb->height;
  frame.opaque = fb;
  frame.ownsData = false;
  diagnostics::value("camera", "raw_width", frame.width);
  diagnostics::value("camera", "raw_height", frame.height);
  diagnostics::size("camera", "raw_bytes", frame.len);
  diagnostics::value("camera", "capture_total_ms", static_cast<long long>(millis() - start));
  return true;
#else
  (void)frame;
  return false;
#endif
}

void release(Frame& frame) {
#ifdef ARDUINO
  if (frame.ownsData && frame.data) {
    std::free(frame.data);
  } else if (frame.opaque) {
    esp_camera_fb_return(static_cast<camera_fb_t*>(frame.opaque));
  }
#endif
  frame = Frame{};
}
}  // namespace camera
