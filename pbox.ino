#include <Adafruit_CircuitPlayground.h>

#include "touch.h"
#include "types.h"


TouchPad tp1 = TouchPad(A1);

auto c_off = CircuitPlayground.strip.Color(0, 0, 0);
auto c_low = CircuitPlayground.strip.Color(30, 30, 30);
auto c_high = CircuitPlayground.strip.Color(255, 255, 255);

void waitForSerial() {
  bool blink_even = false;
  auto next_update = millis();

  while (!CircuitPlayground.slideSwitch() && !Serial) {
    if (millis() >= next_update) {
      next_update += 500;             // blink rate in ms
      blink_even = !blink_even;

      CircuitPlayground.strip.setPixelColor(0, blink_even ? c_high : c_low);
      CircuitPlayground.strip.setPixelColor(9, blink_even ? c_low  : c_high);
      CircuitPlayground.strip.show();
    }
  }
}


void displayTouch(millis_t now) {
  for (int i=0; i<10; ++i) {
    TouchPad::value_t v0 = (i + 0) * 120;
    TouchPad::value_t v1 = (i + 1) * 120;
    TouchPad::value_t v = tp1.max(); // tp1.value();

    auto c = CircuitPlayground.colorWheel(256 * 1200 / v);

    if (v1 <= tp1.min() || tp1.max() < v0)
      c = c_off;
    if (v0 <= v && v < v1)
      c = (v < tp1.threshold()) ? c : c_high;

    CircuitPlayground.strip.setPixelColor(i, c);
  }
}

void displayCalibration(millis_t now) {
  int v = tp1.calibrationTimeLeft(now) / 1000;
  for (int i=0; i<10; ++i)
    CircuitPlayground.strip.setPixelColor(i, i <= v ? c_low : c_off);
}


void setup() {
  // Initialize serial port and circuit playground library.
  CircuitPlayground.begin();

  CircuitPlayground.strip.setBrightness(20);

  Serial.begin(115200);
  waitForSerial();

  auto now = millis();
  tp1.begin(now);
}

const int readingsCount = 10;
uint16_t readings[readingsCount];
int readingsNext = 0;


void loop() {
  auto now = millis();

  if (CircuitPlayground.leftButton())
    tp1.calibrate();

  tp1.loop(now);

  static millis_t neopix_update = 0;
  if (now >= neopix_update) {
    neopix_update = now + 100;

    if (tp1.calibrated())   displayTouch(now);
    else                    displayCalibration(now);

    CircuitPlayground.strip.show();
  }

  if (!plot_touch) {
    static millis_t stats_update = 0;
    if (now >= stats_update) {
      stats_update = now + 1000;
      tp1.printStats(Serial);
    }
  }
}
