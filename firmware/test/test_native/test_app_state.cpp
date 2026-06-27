#include <iostream>

#include "app.h"
#include "buttons.h"

static int failures = 0;

static void expect_state(app::AppState actual, app::AppState expected, const char* message) {
  if (actual != expected) {
    std::cerr << "fail: " << message << "\n";
    ++failures;
  }
}

static void expect_int(int actual, int expected, const char* message) {
  if (actual != expected) {
    std::cerr << "fail: " << message << " expected " << expected << " actual "
              << actual << "\n";
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
  expect_state(ctx.state, app::AppState::Idle, "boot timeout enters idle");
}

static void test_cancel_rules() {
  app::Context waiting = make_context(app::AppState::WaitingForAnswer);
  app::TransitionResult result =
      app::handleButton(waiting, {buttons::Button::Cancel, buttons::EventKind::Down});
  expect_state(waiting.state, app::AppState::Idle, "waiting cancel returns idle");
  if (!result.shouldCancelJob) {
    std::cerr << "fail: waiting cancel requests job cancellation\n";
    ++failures;
  }

  app::Context uploading = make_context(app::AppState::Uploading);
  result = app::handleButton(uploading, {buttons::Button::Cancel, buttons::EventKind::Down});
  expect_state(uploading.state, app::AppState::Idle, "uploading cancel returns idle");
  if (!result.shouldCancelJob) {
    std::cerr << "fail: uploading cancel requests job cancellation\n";
    ++failures;
  }

  app::Context idle = make_context(app::AppState::Idle);
  app::handleButton(idle, {buttons::Button::Cancel, buttons::EventKind::Down});
  expect_state(idle.state, app::AppState::Idle, "idle cancel is ignored");
}

static void test_answer_paging_stops_at_ends() {
  app::Context ctx = make_context(app::AppState::AnswerView);
  ctx.pageIndex = 0;
  app::handleButton(ctx, {buttons::Button::PageUp, buttons::EventKind::Down});
  expect_state(ctx.state, app::AppState::AnswerView, "PageUp stays in answer view");
  if (ctx.pageIndex != 0) {
    std::cerr << "fail: first page PageUp does not wrap\n";
    ++failures;
  }

  app::handleButton(ctx, {buttons::Button::PageDown, buttons::EventKind::Down});
  app::handleButton(ctx, {buttons::Button::PageDown, buttons::EventKind::Repeat});
  app::handleButton(ctx, {buttons::Button::PageDown, buttons::EventKind::Repeat});
  if (ctx.pageIndex != 2) {
    std::cerr << "fail: PageDown stops at last page, actual " << ctx.pageIndex << "\n";
    ++failures;
  }
}

static void test_set_ready_clamps_page_count() {
  app::Context ctx = make_context(app::AppState::WaitingForAnswer);
  ctx.pageIndex = 4;

  app::setReady(ctx, 25);

  expect_int(ctx.pageCount, 20, "setReady clamps pageCount");
  expect_int(ctx.pageIndex, 0, "setReady resets pageIndex");
}

int main() {
  test_boot_timeout_enters_idle();
  test_cancel_rules();
  test_answer_paging_stops_at_ends();
  test_set_ready_clamps_page_count();
  if (failures != 0) {
    std::cerr << failures << " app state tests failed\n";
    return 1;
  }
  std::cout << "app state tests passed\n";
  return 0;
}
