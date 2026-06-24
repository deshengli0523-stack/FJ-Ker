#include "app.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "battery.h"
#include "camera.h"
#include "config.h"
#include "display.h"
#include "net_client.h"
#include "page_store.h"
#include "ui.h"
#endif

namespace app {
namespace {
Context g_ctx;
#ifdef ARDUINO
unsigned long g_bootAt = 0;
unsigned long g_lastPollAt = 0;
unsigned long g_spinnerAt = 0;

void enterPreview() {
  g_ctx.state = AppState::CameraPreview;
  ui::preview();
}

void startWaiting(const uint8_t* jpeg, size_t len) {
  g_ctx.state = AppState::Uploading;
  ui::uploading("正在上传", 0);
  if (!net_client::ensureWifi()) {
    g_ctx.state = AppState::Error;
    ui::error("Wi-Fi 连接失败");
    return;
  }
  std::string jobId;
  if (!net_client::postJobJpeg(jpeg, len, jobId)) {
    g_ctx.state = AppState::Error;
    ui::error("上传失败");
    return;
  }
  g_ctx.activeJobId = jobId;
  g_ctx.state = AppState::WaitingForAnswer;
  g_lastPollAt = 0;
  ui::uploading("正在思考", 0);
}

void pollJob() {
  if (g_ctx.activeJobId.empty()) {
    return;
  }
  net_client::JobStatus status;
  if (!net_client::getJobStatus(g_ctx.activeJobId.c_str(), status)) {
    g_ctx.state = AppState::Error;
    ui::error("轮询失败");
    return;
  }
  if (!status.ready) {
    if (status.error) {
      g_ctx.state = AppState::Error;
      ui::error(status.errorMessage.empty() ? "服务器错误" : status.errorMessage.c_str());
      return;
    }
    ui::uploading("正在思考", static_cast<int>((millis() / 150) % 8));
    return;
  }
  setReady(g_ctx, status.pageCount);
  page_store::reset(g_ctx.pageCount);
  uint8_t* page0 = page_store::ensurePageBuffer(0);
  if (!page0 || !net_client::getPage(g_ctx.activeJobId.c_str(), 0, page0, display::PAGE_BYTES)) {
    g_ctx.state = AppState::Error;
    ui::error("页面获取失败");
    return;
  }
  page_store::markReady(0);
  for (int i = 1; i <= 2 && i < g_ctx.pageCount; ++i) {
    uint8_t* page = page_store::ensurePageBuffer(i);
    if (page) {
      if (net_client::getPage(g_ctx.activeJobId.c_str(), i, page, display::PAGE_BYTES)) {
        page_store::markReady(i);
      }
    }
  }
  g_ctx.state = AppState::AnswerView;
  ui::answer(page_store::get(0), 0, g_ctx.pageCount);
}
#endif
}  // 命名空间

Context& context() { return g_ctx; }

TransitionResult handleButton(Context& ctx, const buttons::ButtonEvent& event) {
  TransitionResult result;
  if (event.kind != buttons::EventKind::Down && event.kind != buttons::EventKind::Repeat) {
    return result;
  }
  if (ctx.state == AppState::LowBattery) {
    return result;
  }

  if (event.which == buttons::Button::Cancel) {
    if (ctx.state == AppState::Idle) {
      return result;
    }
    result.shouldCancelJob = ctx.state == AppState::WaitingForAnswer ||
                             ctx.state == AppState::Uploading;
    ctx.state = AppState::CameraPreview;
    return result;
  }

  switch (ctx.state) {
    case AppState::Idle:
      if (event.which == buttons::Button::Capture) {
        ctx.state = AppState::CameraPreview;
      }
      break;
    case AppState::CameraPreview:
      if (event.which == buttons::Button::Confirm) {
        result.shouldCaptureJpeg = true;
        ctx.state = AppState::Uploading;
      }
      break;
    case AppState::AnswerView:
      if (event.which == buttons::Button::Confirm) {
        ctx.state = AppState::CameraPreview;
      } else if (event.which == buttons::Button::PageUp) {
        if (ctx.pageIndex > 0) {
          --ctx.pageIndex;
          result.shouldFetchPage = true;
        }
      } else if (event.which == buttons::Button::PageDown) {
        if (ctx.pageIndex + 1 < ctx.pageCount) {
          ++ctx.pageIndex;
          result.shouldFetchPage = true;
        }
      }
      break;
    default:
      break;
  }
  return result;
}

void handleTimeout(Context& ctx, TimeoutKind kind) {
  if (kind == TimeoutKind::BootSplash && ctx.state == AppState::Boot) {
    ctx.state = ctx.lowBattery ? AppState::LowBattery : AppState::Idle;
  }
}

void setReady(Context& ctx, int pageCount) {
  ctx.pageCount = pageCount < 0 ? 0 : pageCount;
  ctx.pageIndex = 0;
}

void init() {
#ifdef ARDUINO
  display::init();
  display::clear();
  ui::splash();
  buttons::init();
  camera::initPreview();
  battery::init();
  g_ctx = Context{};
  g_ctx.lowBattery = battery::isLow();
  if (g_ctx.lowBattery) {
    ui::lowBattery(battery::voltage());
  }
  g_bootAt = millis();
#endif
}

void tick() {
#ifdef ARDUINO
  if (g_ctx.state == AppState::Boot && millis() - g_bootAt >= BOOT_SPLASH_MS) {
    handleTimeout(g_ctx, TimeoutKind::BootSplash);
    if (g_ctx.state == AppState::Idle) {
      ui::idle();
    }
    return;
  }

  buttons::ButtonEvent event;
  while (buttons::poll(event)) {
    if (g_ctx.lowBattery &&
        (event.which == buttons::Button::Capture || event.which == buttons::Button::Confirm)) {
      ui::lowBattery(battery::voltage());
      continue;
    }
    const TransitionResult result = handleButton(g_ctx, event);
    if (result.shouldCancelJob && !g_ctx.activeJobId.empty()) {
      net_client::cancelJob(g_ctx.activeJobId.c_str());
      g_ctx.activeJobId.clear();
      enterPreview();
    } else if (result.shouldCaptureJpeg) {
      camera::Frame jpeg;
      if (camera::captureJpeg(jpeg)) {
        startWaiting(jpeg.data, jpeg.len);
        camera::release(jpeg);
      } else {
        g_ctx.state = AppState::Error;
        ui::error("相机失败");
      }
    } else if (result.shouldFetchPage) {
      const uint8_t* page = page_store::get(g_ctx.pageIndex);
      if (!page && !g_ctx.activeJobId.empty()) {
        uint8_t* dst = page_store::ensurePageBuffer(g_ctx.pageIndex);
        if (dst && net_client::getPage(g_ctx.activeJobId.c_str(), g_ctx.pageIndex, dst,
                                       display::PAGE_BYTES)) {
          page_store::markReady(g_ctx.pageIndex);
          page = dst;
        }
      }
      ui::answer(page, g_ctx.pageIndex, g_ctx.pageCount);
    } else if (g_ctx.state == AppState::CameraPreview) {
      enterPreview();
    }
  }

  if (g_ctx.state == AppState::CameraPreview) {
    if (millis() - g_spinnerAt >= 200) {
      g_spinnerAt = millis();
      ui::preview();
    }
  } else if (g_ctx.state == AppState::WaitingForAnswer && millis() - g_lastPollAt >= JOB_POLL_MS) {
    g_lastPollAt = millis();
    pollJob();
  }
#endif
}
}  // 命名空间 app
