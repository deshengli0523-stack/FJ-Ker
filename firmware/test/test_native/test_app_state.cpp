#include <iostream>

#include "app.h"
#include "buttons.h"

static int failures = 0;

static void expect_state(app::AppState actual, app::AppState expected, const char* message) {
  if (actual != expected) {
    std::cerr << "失败: " << message << "\n";
    ++failures;
  }
}

static app::Context make_context(app::AppState state) {
  app::Context ctx;
  ctx.state = state;
  ctx.pageIndex = 0;
  ctx.pageCount = 3;
  ctx.activeJobId = "job-1";
  return ctx;
}

static void test_boot_timeout_enters_idle() {
  app::Context ctx = make_context(app::AppState::Boot);
  app::handleTimeout(ctx, app::TimeoutKind::BootSplash);
  expect_state(ctx.state, app::AppState::Idle, "启动画面超时后进入空闲状态");
}

static void test_low_battery_lockout_cannot_enter_preview() {
  app::Context ctx = make_context(app::AppState::Boot);
  ctx.lowBattery = true;
  app::handleTimeout(ctx, app::TimeoutKind::BootSplash);
  expect_state(ctx.state, app::AppState::LowBattery, "低电量启动后进入锁定状态");

  app::handleButton(ctx, {buttons::Button::Cancel, buttons::EventKind::Down});
  expect_state(ctx.state, app::AppState::LowBattery, "取消键不会退出低电量锁定状态");

  app::handleButton(ctx, {buttons::Button::Capture, buttons::EventKind::Down});
  expect_state(ctx.state, app::AppState::LowBattery, "拍摄键不会退出低电量锁定状态");
}

static void test_cancel_rules() {
  app::Context waiting = make_context(app::AppState::WaitingForAnswer);
  app::TransitionResult result = app::handleButton(waiting, {buttons::Button::Cancel, buttons::EventKind::Down});
  expect_state(waiting.state, app::AppState::CameraPreview, "等待时取消会返回预览");
  if (!result.shouldCancelJob) {
    std::cerr << "失败: 等待时取消会请求取消任务\n";
    ++failures;
  }

  app::Context idle = make_context(app::AppState::Idle);
  app::handleButton(idle, {buttons::Button::Cancel, buttons::EventKind::Down});
  expect_state(idle.state, app::AppState::Idle, "空闲时取消不执行操作");
}

static void test_answer_paging_stops_at_ends() {
  app::Context ctx = make_context(app::AppState::AnswerView);
  ctx.pageIndex = 0;
  app::handleButton(ctx, {buttons::Button::PageUp, buttons::EventKind::Down});
  expect_state(ctx.state, app::AppState::AnswerView, "PageUp 后仍停留在答案视图");
  if (ctx.pageIndex != 0) {
    std::cerr << "失败: 第一页按 PageUp 不会回绕\n";
    ++failures;
  }

  app::handleButton(ctx, {buttons::Button::PageDown, buttons::EventKind::Down});
  app::handleButton(ctx, {buttons::Button::PageDown, buttons::EventKind::Repeat});
  app::handleButton(ctx, {buttons::Button::PageDown, buttons::EventKind::Repeat});
  if (ctx.pageIndex != 2) {
    std::cerr << "失败: PageDown 会停在最后一页，实际为 " << ctx.pageIndex << "\n";
    ++failures;
  }
}

int main() {
  test_boot_timeout_enters_idle();
  test_low_battery_lockout_cannot_enter_preview();
  test_cancel_rules();
  test_answer_paging_stops_at_ends();
  if (failures != 0) {
    std::cerr << failures << " 个应用状态测试失败\n";
    return 1;
  }
  std::cout << "应用状态测试通过\n";
  return 0;
}
