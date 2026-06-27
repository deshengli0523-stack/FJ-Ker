#include "page_store.h"

#include <cstdlib>
#include <cstring>

#include "diagnostics.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "display.h"
#else
#include "display.h"
#endif

namespace page_store {
namespace {
uint8_t* g_pages[MAX_PAGES] = {};
bool g_ready[MAX_PAGES] = {};
int g_count = 0;

uint8_t* allocatePage() {
#ifdef ARDUINO
  if (psramFound()) {
    return static_cast<uint8_t*>(ps_malloc(display::PAGE_BYTES));
  }
#endif
  return static_cast<uint8_t*>(std::malloc(display::PAGE_BYTES));
}
}  // 命名空间

int clampPageCount(int pageCount) {
  return pageCount < 0 ? 0 : (pageCount > MAX_PAGES ? MAX_PAGES : pageCount);
}

void clear() {
  bool hadState = g_count != 0;
  for (int i = 0; i < MAX_PAGES; ++i) {
    hadState = hadState || g_pages[i] != nullptr || g_ready[i];
    std::free(g_pages[i]);
    g_pages[i] = nullptr;
    g_ready[i] = false;
  }
  if (hadState) {
    diagnostics::event("pages", "clear");
  }
  g_count = 0;
}

void reset(int pageCount) {
  clear();
  g_count = clampPageCount(pageCount);
  diagnostics::value("pages", "requested_count", pageCount);
  diagnostics::value("pages", "active_count", g_count);
}

uint8_t* ensurePageBuffer(int index) {
  if (index < 0 || index >= g_count || index >= MAX_PAGES) {
    diagnostics::value("pages", "invalid_index", index);
    diagnostics::value("pages", "active_count", g_count);
    return nullptr;
  }
  if (!g_pages[index]) {
    g_pages[index] = allocatePage();
    if (g_pages[index]) {
      std::memset(g_pages[index], 0, display::PAGE_BYTES);
      diagnostics::value("pages", "alloc_index", index);
    } else {
      diagnostics::value("pages", "alloc_failed_index", index);
    }
  }
  return g_pages[index];
}

void markReady(int index) {
  if (index >= 0 && index < g_count && index < MAX_PAGES) {
    g_ready[index] = true;
    diagnostics::value("pages", "ready_index", index);
  }
}

const uint8_t* get(int index) {
  if (index < 0 || index >= g_count || index >= MAX_PAGES) {
    return nullptr;
  }
#ifdef ARDUINO
  const unsigned long start = millis();
  while (!g_ready[index] && millis() - start < 2000) {
    delay(10);
  }
  if (!g_ready[index]) {
    diagnostics::value("pages", "wait_timeout_index", index);
  }
#endif
  return g_ready[index] ? g_pages[index] : nullptr;
}

int count() { return g_count; }
}  // 命名空间 page_store
