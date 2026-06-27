#include <cstring>
#include <iostream>

#include "diagnostics.h"

static int failures = 0;

static void expect_str(const char* actual, const char* expected, const char* message) {
  if (std::strcmp(actual, expected) != 0) {
    std::cerr << "fail: " << message << " expected '" << expected << "' actual '"
              << actual << "'\n";
    ++failures;
  }
}

static void test_format_event_line() {
  char line[96];
  diagnostics::formatEvent(line, sizeof(line), "camera", "init failed");

  expect_str(line, "[FJKER][camera] init failed", "formatEvent includes tag and area");
}

static void test_format_value_line() {
  char line[96];
  diagnostics::formatValue(line, sizeof(line), "net", "http", -1);

  expect_str(line, "[FJKER][net] http=-1", "formatValue includes signed value");
}

static void test_format_text_line() {
  char line[96];
  diagnostics::formatText(line, sizeof(line), "job", "id", "abc123");

  expect_str(line, "[FJKER][job] id=abc123", "formatText includes string value");
}

static void test_format_size_line() {
  char line[96];
  diagnostics::formatSize(line, sizeof(line), "capture", "jpeg", 123456);

  expect_str(line, "[FJKER][capture] jpeg=123456", "formatSize includes byte count");
}

static void test_format_token_line_masks_prefix() {
  char line[96];
  diagnostics::formatToken(line, sizeof(line), "job", "id", "abcdefghijk");

  expect_str(line, "[FJKER][job] id=*fghijk", "formatToken masks all but suffix");
}

int main() {
  test_format_event_line();
  test_format_value_line();
  test_format_text_line();
  test_format_size_line();
  test_format_token_line_masks_prefix();

  if (failures != 0) {
    std::cerr << failures << " diagnostics tests failed\n";
    return 1;
  }
  std::cout << "diagnostics tests passed\n";
  return 0;
}
