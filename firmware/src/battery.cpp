#include "battery.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "config.h"
#include "pins.h"
#endif

namespace battery {
void init() {
#ifdef ARDUINO
  analogReadResolution(12);
  analogSetPinAttenuation(pins::VBAT_SENSE, ADC_11db);
#endif
}

float voltage() {
#ifdef ARDUINO
  const int raw = analogRead(pins::VBAT_SENSE);
  const float adc = (static_cast<float>(raw) / 4095.0f) * 3.3f;
  constexpr float dividerRatio = 2.0f;
  return adc * dividerRatio;
#else
  return 4.0f;
#endif
}

bool isLow() {
#ifdef ARDUINO
  return voltage() < LOW_BATTERY_VOLTS;
#else
  return false;
#endif
}
}  // 命名空间 battery
