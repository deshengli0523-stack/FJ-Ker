#pragma once

#include <cstddef>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASS ""
#define SERVER_BASE_URL "http://192.168.1.42:8080"
#define FJKER_API_TOKEN ""
#warning "请将 include/secrets.h.example 复制为 include/secrets.h，并填写 Wi-Fi/服务器配置。"
#endif

constexpr unsigned long BOOT_SPLASH_MS = 1500;
constexpr unsigned long JOB_POLL_MS = 1500;
constexpr float LOW_BATTERY_VOLTS = 3.3f;
constexpr size_t FJKER_MAX_UPLOAD_BYTES = 6000000;
constexpr size_t FJKER_MAX_JPEG_BYTES = FJKER_MAX_UPLOAD_BYTES;
