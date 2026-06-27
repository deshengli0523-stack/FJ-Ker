#include "battery.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace battery {
namespace {
constexpr bool kAdcEnabled = false;
}

void init() {
#ifdef ARDUINO
  if (kAdcEnabled) {
    analogReadResolution(12);
  }
#endif
}

float voltage() {
#ifdef ARDUINO
  if (!kAdcEnabled) {
    return 4.0f;
  }
#endif
  return 4.0f;
}

bool isLow() {
  return voltage() < 3.3f;
}
}  // namespace battery
