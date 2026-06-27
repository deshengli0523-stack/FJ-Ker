#include <cstdint>
#include <iostream>

#include "page_store.h"

static int failures = 0;

static void expect_true(bool value, const char* message) {
  if (!value) {
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

static void test_clear_releases_pages_and_resets_readiness() {
  page_store::reset(2);
  uint8_t* page = page_store::ensurePageBuffer(0);
  expect_true(page != nullptr, "reset allows page allocation");
  page_store::markReady(0);
  expect_true(page_store::get(0) != nullptr, "ready page can be read");

  page_store::clear();

  expect_int(page_store::count(), 0, "clear sets count to zero");
  expect_true(page_store::get(0) == nullptr, "clear makes old page unreadable");
  expect_true(page_store::ensurePageBuffer(0) == nullptr,
              "clear makes old index unavailable");
}

static void test_reset_clamps_to_max_pages() {
  page_store::reset(25);

  expect_int(page_store::count(), page_store::MAX_PAGES,
             "reset clamps count to MAX_PAGES");
  expect_true(page_store::ensurePageBuffer(page_store::MAX_PAGES - 1) != nullptr,
              "last clamped page can be allocated");
  expect_true(page_store::ensurePageBuffer(page_store::MAX_PAGES) == nullptr,
              "page past clamp cannot be allocated");
}

static void test_reset_and_ensure_do_not_keep_old_ready_state() {
  page_store::reset(1);
  uint8_t* first = page_store::ensurePageBuffer(0);
  expect_true(first != nullptr, "initial reset allows page allocation");
  page_store::markReady(0);
  expect_true(page_store::get(0) != nullptr, "initial ready page can be read");

  page_store::reset(1);
  uint8_t* second = page_store::ensurePageBuffer(0);

  expect_true(second != nullptr, "repeated reset allows page allocation");
  expect_true(page_store::get(0) == nullptr,
              "repeated reset/ensurePageBuffer does not keep old ready state");
}

int main() {
  test_clear_releases_pages_and_resets_readiness();
  test_reset_clamps_to_max_pages();
  test_reset_and_ensure_do_not_keep_old_ready_state();
  page_store::clear();

  if (failures != 0) {
    std::cerr << failures << " page_store tests failed\n";
    return 1;
  }
  std::cout << "page_store tests passed\n";
  return 0;
}
