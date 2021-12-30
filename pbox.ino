#include <Adafruit_CircuitPlayground.h>

#include "dmadac.h"
#include "filemanager.h"
#include "sound.h"
#include "touch.h"
#include "types.h"


TouchPad tp1 = TouchPad(A1);

// TriangleToneSource tri;
SampleSource samp;


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
  if (!CircuitPlayground.slideSwitch())
    delay(1000);
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

extern "C" char* sbrk(int incr);

uint32_t sramUsed() {
  return (uint32_t)(sbrk(0)) - 0x20000000;
}


void setup() {
  auto s0 = sramUsed();

  // Initialize serial port and circuit playground library.
  CircuitPlayground.begin();

  CircuitPlayground.strip.setBrightness(20);

  bool fmSetup = FileManager::setup();

  Serial.begin(115200);
  waitForSerial();
  Serial.println();
  Serial.println("> : ~ : .. : Pandora's Drumming Box : .. : ~ : <");
  Serial.println();
  Serial.flush();

  if (fmSetup)
    Serial.println("FileManager is setup and happy!");
  samp.load("right");

  auto now = millis();

  DmaDac::begin();
  DmaDac::setSource(samp);

  tp1.begin(now);

  auto s1 = sramUsed();
  Serial.printf("sram used: %d static, %d post-init\n", s0, s1);
}

const int readingsCount = 10;
uint16_t readings[readingsCount];
int readingsNext = 0;


void loop() {
  FileManager::loop();

  auto now = millis();

  if (CircuitPlayground.leftButton())
    tp1.calibrate();

  tp1.loop(now);

  if (tp1.calibrated()) {
    static bool pressed = false;
    if (!pressed) {
      if (tp1.max() >= tp1.threshold()) {
        pressed = true;
        // tri.playNote(600.0f, 0.5f);
        samp.play(0.8f);
      }
    } else {
      if (tp1.max() < tp1.threshold()) {
        pressed = false;
      }
    }
  }

  static millis_t neopix_update = 0;
  if (now >= neopix_update) {
    neopix_update = now + 100;

    if (tp1.calibrated())   displayTouch(now);
    else                    displayCalibration(now);

    CircuitPlayground.strip.show();
  }

#if 0
  if (!plot_touch) {
    static millis_t stats_update = 0;
    if (now >= stats_update) {
      stats_update = now + 1000;
      tp1.printStats(Serial);
      DmaDac::report(Serial);
    }
  }
#endif
}
