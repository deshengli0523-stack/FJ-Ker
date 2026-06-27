#include "diagnostics.h"

#include <cstdio>
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace diagnostics {
namespace {
constexpr size_t LINE_BYTES = 128;

void emit(const char* line) {
#if defined(ARDUINO) && FJKER_ENABLE_DIAGNOSTICS
  Serial.println(line);
#else
  (void)line;
#endif
}
}  // namespace

void formatEvent(char* dst, size_t dstLen, const char* area, const char* message) {
  std::snprintf(dst, dstLen, "[FJKER][%s] %s", area ? area : "-", message ? message : "-");
}

void formatText(char* dst, size_t dstLen, const char* area, const char* key,
                const char* value) {
  std::snprintf(dst, dstLen, "[FJKER][%s] %s=%s", area ? area : "-",
                key ? key : "-", value ? value : "-");
}

void formatValue(char* dst, size_t dstLen, const char* area, const char* key,
                 long long value) {
  std::snprintf(dst, dstLen, "[FJKER][%s] %s=%lld", area ? area : "-",
                key ? key : "-", value);
}

void formatSize(char* dst, size_t dstLen, const char* area, const char* key,
                size_t value) {
  std::snprintf(dst, dstLen, "[FJKER][%s] %s=%llu", area ? area : "-",
                key ? key : "-", static_cast<unsigned long long>(value));
}

void formatToken(char* dst, size_t dstLen, const char* area, const char* key,
                 const char* value) {
  if (!value || value[0] == '\0') {
    formatText(dst, dstLen, area, key, "-");
    return;
  }
  const size_t len = std::strlen(value);
  const char* suffix = len > 6 ? value + len - 6 : value;
  std::snprintf(dst, dstLen, "[FJKER][%s] %s=*%s", area ? area : "-",
                key ? key : "-", suffix);
}

void event(const char* area, const char* message) {
  char line[LINE_BYTES];
  formatEvent(line, sizeof(line), area, message);
  emit(line);
}

void text(const char* area, const char* key, const char* value) {
  char line[LINE_BYTES];
  formatText(line, sizeof(line), area, key, value);
  emit(line);
}

void token(const char* area, const char* key, const char* value) {
  char line[LINE_BYTES];
  formatToken(line, sizeof(line), area, key, value);
  emit(line);
}

void value(const char* area, const char* key, long long value) {
  char line[LINE_BYTES];
  formatValue(line, sizeof(line), area, key, value);
  emit(line);
}

void size(const char* area, const char* key, size_t value) {
  char line[LINE_BYTES];
  formatSize(line, sizeof(line), area, key, value);
  emit(line);
}

void result(const char* area, const char* action, bool ok) {
  text(area, action, ok ? "ok" : "failed");
}
}  // namespace diagnostics
