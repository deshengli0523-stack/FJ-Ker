#include "app.h"

#include "diagnostics.h"
#include "page_store.h"

#include <string>

#ifdef ARDUINO
#include <Arduino.h>
#include "camera.h"
#include "config.h"
#include "display.h"
#include "net_client.h"
#include "ui.h"
#endif

#ifndef FJKER_BUTTON_RAW_DIAGNOSTICS
#define FJKER_BUTTON_RAW_DIAGNOSTICS 0
#endif

namespace app {
namespace {
Context g_ctx;
constexpr unsigned long PREVIEW_FRAME_MS = 120;

void clearPageState(Context& ctx) {
  diagnostics::event("state", "clear page state");
  page_store::clear();
  ctx.pageIndex = 0;
  ctx.pageCount = 0;
}

#ifdef ARDUINO
unsigned long g_bootAt = 0;
unsigned long g_lastPollAt = 0;
unsigned long g_spinnerAt = 0;
unsigned long g_lastButtonRawAt = 0;

const char* buttonName(buttons::Button button) {
  switch (button) {
    case buttons::Button::Capture:
      return "capture";
    case buttons::Button::Confirm:
      return "confirm";
    case buttons::Button::Cancel:
      return "cancel";
    case buttons::Button::PageUp:
      return "page_up";
    case buttons::Button::PageDown:
      return "page_down";
  }
  return "unknown";
}

const char* eventKindName(buttons::EventKind kind) {
  switch (kind) {
    case buttons::EventKind::Down:
      return "down";
    case buttons::EventKind::Up:
      return "up";
    case buttons::EventKind::Repeat:
      return "repeat";
  }
  return "unknown";
}

void enterPreview() {
  diagnostics::event("state", "enter preview");
  g_ctx.state = AppState::CameraPreview;
  g_spinnerAt = millis();
  ui::preview();
}

bool connectWifiForUpload() {
  clearPageState(g_ctx);
  g_ctx.state = AppState::Uploading;
  ui::uploading("WIFI", 0);
  if (!net_client::ensureWifi()) {
    diagnostics::event("state", "wifi failed");
    clearPageState(g_ctx);
    g_ctx.state = AppState::Error;
    ui::error("WIFI FAIL");
    return false;
  }
  return true;
}

void startWaiting(const camera::Frame& image) {
  diagnostics::size("state", "upload_raw_bytes", image.len);
  clearPageState(g_ctx);
  g_ctx.state = AppState::Uploading;
  ui::uploading("UPLOADING", 0);
  std::string jobId;
  if (!net_client::postJobRawRgb565(image.data, image.len, image.width, image.height, jobId)) {
    diagnostics::event("state", "upload failed");
    clearPageState(g_ctx);
    g_ctx.state = AppState::Error;
    ui::error("UPLOAD FAIL");
    return;
  }
  g_ctx.activeJobId = jobId;
  diagnostics::token("state", "active_job", g_ctx.activeJobId.c_str());
  g_ctx.state = AppState::WaitingForAnswer;
  g_lastPollAt = 0;
  ui::uploading("WAITING", 0);
}

void pollJob() {
  if (g_ctx.activeJobId.empty()) {
    diagnostics::event("state", "poll skipped without job");
    return;
  }
  net_client::JobStatus status;
  if (!net_client::getJobStatus(g_ctx.activeJobId.c_str(), status)) {
    diagnostics::event("state", "poll failed");
    clearPageState(g_ctx);
    g_ctx.state = AppState::Error;
    ui::error("POLL FAIL");
    return;
  }
  if (!status.ready) {
    if (status.error) {
      diagnostics::event("state", "server returned error");
      clearPageState(g_ctx);
      g_ctx.state = AppState::Error;
      ui::error(status.errorMessage.empty() ? "SERVER ERR" : status.errorMessage.c_str());
      return;
    }
    ui::uploading("WAITING", static_cast<int>((millis() / 150) % 8));
    return;
  }
  diagnostics::value("state", "ready_pages", status.pageCount);
  clearPageState(g_ctx);
  setReady(g_ctx, status.pageCount);
  page_store::reset(g_ctx.pageCount);
  uint8_t* page0 = page_store::ensurePageBuffer(0);
  if (!page0 || !net_client::getPage(g_ctx.activeJobId.c_str(), 0, page0, display::PAGE_BYTES)) {
    diagnostics::event("state", "first page fetch failed");
    clearPageState(g_ctx);
    g_ctx.state = AppState::Error;
    ui::error("PAGE FAIL");
    return;
  }
  page_store::markReady(0);
  for (int i = 1; i <= 2 && i < g_ctx.pageCount; ++i) {
    uint8_t* page = page_store::ensurePageBuffer(i);
    if (page) {
      if (net_client::getPage(g_ctx.activeJobId.c_str(), i, page, display::PAGE_BYTES)) {
        page_store::markReady(i);
      } else {
        diagnostics::value("state", "prefetch_failed_index", i);
      }
    }
  }
  g_ctx.state = AppState::AnswerView;
  ui::answer(page_store::get(0), 0, g_ctx.pageCount);
}
#endif
}  // namespace

Context& context() { return g_ctx; }

TransitionResult handleButton(Context& ctx, const buttons::ButtonEvent& event) {
  TransitionResult result;
  if (event.kind != buttons::EventKind::Down && event.kind != buttons::EventKind::Repeat) {
    return result;
  }
  if (event.which == buttons::Button::Cancel) {
    diagnostics::event("button", "cancel");
    if (ctx.state == AppState::Idle) {
      return result;
    }
    result.shouldCancelJob = ctx.state == AppState::WaitingForAnswer ||
                             ctx.state == AppState::Uploading;
    clearPageState(ctx);
    ctx.state = AppState::Idle;
    return result;
  }

  switch (ctx.state) {
    case AppState::Idle:
      if (event.which == buttons::Button::Capture) {
        diagnostics::event("button", "capture opens preview");
        ctx.state = AppState::CameraPreview;
      }
      break;
    case AppState::CameraPreview:
      if (event.which == buttons::Button::Confirm) {
        diagnostics::event("button", "confirm capture");
        clearPageState(ctx);
        result.shouldCaptureRaw = true;
        ctx.state = AppState::Uploading;
      }
      break;
    case AppState::AnswerView:
      if (event.which == buttons::Button::Confirm) {
        diagnostics::event("button", "confirm return preview");
        clearPageState(ctx);
        ctx.state = AppState::CameraPreview;
      } else if (event.which == buttons::Button::PageUp) {
        if (ctx.pageIndex > 0) {
          --ctx.pageIndex;
          diagnostics::value("button", "page_up_to", ctx.pageIndex);
          result.shouldFetchPage = true;
        }
      } else if (event.which == buttons::Button::PageDown) {
        if (ctx.pageIndex + 1 < ctx.pageCount) {
          ++ctx.pageIndex;
          diagnostics::value("button", "page_down_to", ctx.pageIndex);
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
    ctx.state = AppState::Idle;
    diagnostics::text("state", "boot_timeout", "idle");
  }
}

void setReady(Context& ctx, int pageCount) {
  ctx.pageCount = page_store::clampPageCount(pageCount);
  ctx.pageIndex = 0;
  diagnostics::value("state", "clamped_pages", ctx.pageCount);
}

void init() {
#ifdef ARDUINO
  diagnostics::event("state", "init start");
  display::init();
  display::clear();
  ui::splash();
  buttons::init();
  const bool previewOk = camera::initPreview();
  diagnostics::result("state", "preview_init", previewOk);
  g_ctx = Context{};
  g_bootAt = millis();
#endif
}

void tick() {
#ifdef ARDUINO
#if FJKER_BUTTON_RAW_DIAGNOSTICS
  if (millis() - g_lastButtonRawAt >= 1000) {
    g_lastButtonRawAt = millis();
    buttons::logRawLevels();
  }
#endif

  if (g_ctx.state == AppState::Boot && millis() - g_bootAt >= BOOT_SPLASH_MS) {
    handleTimeout(g_ctx, TimeoutKind::BootSplash);
    if (g_ctx.state == AppState::Idle) {
      ui::idle();
    }
    return;
  }

  buttons::ButtonEvent event;
  while (buttons::poll(event)) {
    diagnostics::text("button", "which", buttonName(event.which));
    diagnostics::text("button", "kind", eventKindName(event.kind));
    const AppState stateBeforeButton = g_ctx.state;
    const TransitionResult result = handleButton(g_ctx, event);
    if (result.shouldCancelJob && !g_ctx.activeJobId.empty()) {
      diagnostics::token("state", "cancel_job", g_ctx.activeJobId.c_str());
      net_client::cancelJob(g_ctx.activeJobId.c_str());
      g_ctx.activeJobId.clear();
      diagnostics::event("state", "active_job cleared");
      ui::idle();
    } else if (result.shouldCaptureRaw) {
      diagnostics::event("state", "wifi requested before capture");
      if (!connectWifiForUpload()) {
        continue;
      }
      diagnostics::event("state", "capture requested");
      ui::uploading("CAPTURE", 0);
      camera::Frame raw;
      if (camera::captureRaw(raw)) {
        if (raw.len > FJKER_MAX_RAW_IMAGE_BYTES) {
          diagnostics::size("state", "oversize_raw", raw.len);
          diagnostics::size("state", "max_raw_bytes", FJKER_MAX_RAW_IMAGE_BYTES);
          camera::release(raw);
          clearPageState(g_ctx);
          g_ctx.state = AppState::Error;
          ui::error("IMAGE TOO BIG");
          continue;
        }
        diagnostics::event("state", "capture complete");
        startWaiting(raw);
        camera::release(raw);
      } else {
        diagnostics::event("state", "capture failed");
        clearPageState(g_ctx);
        g_ctx.state = AppState::Error;
        ui::error("CAMERA FAIL");
      }
    } else if (result.shouldFetchPage) {
      const uint8_t* page = page_store::get(g_ctx.pageIndex);
      if (!page && !g_ctx.activeJobId.empty()) {
        diagnostics::value("state", "fetch_page", g_ctx.pageIndex);
        uint8_t* dst = page_store::ensurePageBuffer(g_ctx.pageIndex);
        if (dst && net_client::getPage(g_ctx.activeJobId.c_str(), g_ctx.pageIndex, dst,
                                       display::PAGE_BYTES)) {
          page_store::markReady(g_ctx.pageIndex);
          page = dst;
        } else {
          diagnostics::value("state", "fetch_page_failed", g_ctx.pageIndex);
        }
      }
      ui::answer(page, g_ctx.pageIndex, g_ctx.pageCount);
    } else if (g_ctx.state == AppState::CameraPreview &&
               stateBeforeButton != AppState::CameraPreview) {
      enterPreview();
    } else if (g_ctx.state == AppState::Idle && stateBeforeButton != AppState::Idle) {
      ui::idle();
    }
  }

  if (g_ctx.state == AppState::CameraPreview) {
    if (millis() - g_spinnerAt >= PREVIEW_FRAME_MS) {
      g_spinnerAt = millis();
      ui::preview();
    }
  } else if (g_ctx.state == AppState::WaitingForAnswer && millis() - g_lastPollAt >= JOB_POLL_MS) {
    g_lastPollAt = millis();
    pollJob();
  }
#endif
}
}  // namespace app
