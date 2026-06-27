#include <Arduino.h>

#include "app.h"
#include "diagnostics.h"

#ifdef FJKER_DISPLAY_TEST
#include "display_test.h"
#endif

void setup() {
  Serial.begin(115200);
  delay(50);
#ifdef FJKER_DISPLAY_TEST
  display_test::setup();
#else
  diagnostics::event("boot", "serial ready");
  app::init();
#endif
}

void loop() {
#ifdef FJKER_DISPLAY_TEST
  display_test::tick();
#else
  app::tick();
#endif
  delay(1);
}
