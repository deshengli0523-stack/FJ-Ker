#include "page_store.h"

#include <cstdlib>
#include <cstring>

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

void reset(int pageCount) {
  for (int i = 0; i < MAX_PAGES; ++i) {
    g_ready[i] = false;
  }
  g_count = pageCount < 0 ? 0 : (pageCount > MAX_PAGES ? MAX_PAGES : pageCount);
}

uint8_t* ensurePageBuffer(int index) {
  if (index < 0 || index >= g_count || index >= MAX_PAGES) {
    return nullptr;
  }
  if (!g_pages[index]) {
    g_pages[index] = allocatePage();
    if (g_pages[index]) {
      std::memset(g_pages[index], 0, display::PAGE_BYTES);
    }
  }
  return g_pages[index];
}

void markReady(int index) {
  if (index >= 0 && index < g_count && index < MAX_PAGES) {
    g_ready[index] = true;
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
#endif
  return g_ready[index] ? g_pages[index] : nullptr;
}

int count() { return g_count; }
}  // 命名空间 page_store
