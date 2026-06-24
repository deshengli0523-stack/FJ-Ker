#include "buttons.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pins.h"
#endif

namespace buttons {
#ifdef ARDUINO
namespace {
struct ButtonSlot {
  Button which;
  int pin;
  bool stableDown = false;
  uint8_t history = 0;
  uint32_t lastRepeatMs = 0;
};

ButtonSlot g_slots[] = {
    {Button::Capture, pins::BTN_CAPTURE},
    {Button::Confirm, pins::BTN_CONFIRM},
    {Button::Cancel, pins::BTN_CANCEL},
    {Button::PageUp, pins::BTN_PAGE_UP},
    {Button::PageDown, pins::BTN_PAGE_DOWN},
};
QueueHandle_t g_queue = nullptr;

bool repeats(Button b) { return b == Button::PageUp || b == Button::PageDown; }

void enqueue(Button which, EventKind kind) {
  ButtonEvent ev{which, kind};
  xQueueSend(g_queue, &ev, 0);
}

void task(void*) {
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    for (auto& slot : g_slots) {
      const bool down = digitalRead(slot.pin) == LOW;
      slot.history = static_cast<uint8_t>(((slot.history << 1) | (down ? 1 : 0)) & 0x07);
      if (slot.history == 0x07 && !slot.stableDown) {
        slot.stableDown = true;
        slot.lastRepeatMs = now;
        enqueue(slot.which, EventKind::Down);
      } else if (slot.history == 0x00 && slot.stableDown) {
        slot.stableDown = false;
        enqueue(slot.which, EventKind::Up);
      } else if (slot.stableDown && repeats(slot.which) && now - slot.lastRepeatMs >= 200) {
        slot.lastRepeatMs = now;
        enqueue(slot.which, EventKind::Repeat);
      }
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(5));
  }
}
}  // 命名空间
#endif

void init() {
#ifdef ARDUINO
  if (!g_queue) {
    g_queue = xQueueCreate(16, sizeof(ButtonEvent));
  }
  for (const auto& slot : g_slots) {
    pinMode(slot.pin, INPUT_PULLUP);
  }
  xTaskCreatePinnedToCore(task, "buttons", 3072, nullptr, 2, nullptr, 1);
#endif
}

bool poll(ButtonEvent& event) {
#ifdef ARDUINO
  return g_queue && xQueueReceive(g_queue, &event, 0) == pdTRUE;
#else
  (void)event;
  return false;
#endif
}
}  // 命名空间 buttons
