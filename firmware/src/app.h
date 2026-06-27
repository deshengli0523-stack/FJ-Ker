#pragma once

#include <cstdint>
#include <string>

#include "buttons.h"

namespace app {
enum class AppState : uint8_t {
  Boot,
  Idle,
  CameraPreview,
  Uploading,
  WaitingForAnswer,
  AnswerView,
  Error
};

enum class TimeoutKind : uint8_t { BootSplash, Poll };

struct Context {
  AppState state = AppState::Boot;
  int pageIndex = 0;
  int pageCount = 0;
  std::string activeJobId;
};

struct TransitionResult {
  bool shouldCaptureRaw = false;
  bool shouldCancelJob = false;
  bool shouldFetchPage = false;
};

void init();
void tick();
TransitionResult handleButton(Context& ctx, const buttons::ButtonEvent& event);
void handleTimeout(Context& ctx, TimeoutKind kind);
void setReady(Context& ctx, int pageCount);
Context& context();
}  // 命名空间 app
