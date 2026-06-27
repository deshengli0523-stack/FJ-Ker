#pragma once

#include <cstdint>

namespace buttons {
enum class Button : uint8_t { Capture, Confirm, Cancel, PageUp, PageDown };
enum class EventKind : uint8_t { Down, Up, Repeat };

struct ButtonEvent {
  Button which;
  EventKind kind;
};

void init();
bool poll(ButtonEvent& event);
void logRawLevels();
}  // 命名空间 buttons
