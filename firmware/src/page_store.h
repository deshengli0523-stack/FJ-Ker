#pragma once

#include <cstdint>

namespace page_store {
constexpr int MAX_PAGES = 20;

void reset(int pageCount);
void clear();
int clampPageCount(int pageCount);
uint8_t* ensurePageBuffer(int index);
void markReady(int index);
const uint8_t* get(int index);
int count();
}  // 命名空间 page_store
