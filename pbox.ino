// NB: For best timinigs, compile whole program with -O2
//    Annotating individual functions with -O3 or higher, makes them slower!

#include <Adafruit_CircuitPlayground.h>

#include "dmadac.h"
#include "filemanager.h"
#include "sound.h"
#include "touch.h"
#include "types.h"

const char* fileSuffix = "24k8.raw";
const int file_sample_rate = 24000;

TouchPad tp1 = TouchPad(A1);
TouchPad tp2 = TouchPad(A2);

// TriangleToneSource tri;
// SampleSource samp;

SampleGateSource<file_sample_rate> gate1;
SampleGateSource<file_sample_rate> gate2;
MixSource mix(gate1, gate2);
FilterSource filt(mix);

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

void displayTouchPixel(int i,
  TouchPad::value_t v0, TouchPad::value_t v1, const TouchPad& tp)
{
  if (v1 <= tp.min() || tp.max() < v0)
    return;

  TouchPad::value_t v = tp.max(); // tp.value();
  auto c = CircuitPlayground.colorWheel(256 * 1200 / v);

  if (v0 <= v && v < v1)
    c = (v < tp.threshold()) ? c : c_high;

  CircuitPlayground.strip.setPixelColor(i, c);
}

void displayTouch(millis_t now) {
  CircuitPlayground.strip.clear();

  for (int i=0; i<10; ++i) {
    TouchPad::value_t v0 = (i + 0) * 120;
    TouchPad::value_t v1 = (i + 1) * 120;

    displayTouchPixel(i, v0, v1, tp1);
    displayTouchPixel(9-i, v0, v1, tp2);
  }
}

void displayCalibration(millis_t now) {
  int v = tp1.calibrationTimeLeft(now) / 1000;
  for (int i=0; i<10; ++i)
    CircuitPlayground.strip.setPixelColor(i, i <= v ? c_high : c_off);
}


bool sweepLoop(millis_t now) {
  static bool sweepingStarted = false;
  bool sweeping = CircuitPlayground.rightButton();

  if (sweeping) {
    static millis_t update = 0;
    if (now >= update) {
      update = now + 100;

      const float cfMin = 0.0f;
      const float cfMax = 5.0f;
      const float cfInc = 0.0625f;

      const float qMin = 0.1f;
      const float qMax = 0.9f;
      const float qInc = 0.2f;

      static float cf = cfMin;
      static float q = qMin;

      if (!sweepingStarted) {
        sweepingStarted = true;
        cf = cfMin;
        q = qMin;

        Serial.println("sweeping filter over left sound");
        Serial.print("q = ");
        Serial.println(q);
        Serial.flush();
      }

      filt.setFreqAndQ(55.0f*expf(cf), q);
      gate1.gate(100, 0, 100, 10);

      cf += cfInc;
      if (cf > cfMax) {
        cf = cfMin;

        q += qInc;
        if (q > qMax) {
          q = qMin;
        }

        Serial.print("q = ");
        Serial.println(q);
        Serial.flush();
      }
    }
  }
  else
    sweepingStarted = false;

  return sweeping;
}



extern "C" char* sbrk(int incr);

uint32_t sramUsed() {
  return (uint32_t)(sbrk(0)) - 0x20000000;
}


void setup() {
  auto s0 = sramUsed();

  // Initialize serial port and circuit playground library.
  CircuitPlayground.begin();
  CircuitPlayground.strip.setBrightness(5);

  bool fmSetup = FileManager::setupFileSystem();

  Serial.begin(115200);
  waitForSerial();
  Serial.println();
  Serial.println("> : ~ : .. : Pandora's Drumming Box : .. : ~ : <");
  Serial.println();
  Serial.flush();

  if (fmSetup) {
    Serial.println("FileManager is setup and happy!");
  }
  FileManager::showMessages();

  if (fmSetup) {
    FileManager::SampleFiles sf;
    if (FileManager::locateFiles(sf, fileSuffix)) {
    gate1.load(Samples(sf.leftData, sf.leftSize));
    gate2.load(Samples(sf.rightData, sf.rightSize));
  }
  }

  auto now = millis();

  DmaDac::begin();
  DmaDac::setSource(filt);

  tp1.begin(now);
  tp2.begin(now);

  auto s1 = sramUsed();
  Serial.printf("sram used: %d static, %d post-init\n", s0, s1);
}

const int readingsCount = 10;
uint16_t readings[readingsCount];
int readingsNext = 0;


void loop() {
  auto now = millis();

  if (CircuitPlayground.leftButton()) {
    tp1.calibrate();
    tp1.calibrate();
  }

  tp1.loop(now);
  tp2.loop(now);

  // if (sweepLoop(now)) return;

  if (tp1.calibrated()) {
    gate1.gate(tp1.max(),
      TouchPad::value_t(200), TouchPad::value_t(800), tp1.threshold());
  }
  if (tp2.calibrated()) {
    gate2.gate(tp2.max(),
      TouchPad::value_t(200), TouchPad::value_t(800), tp2.threshold());
  }

  static millis_t accel_update = 0;
  if (now >= accel_update) {
    accel_update = now + 100;

    auto y = CircuitPlayground.motionY();
    float f = 20.0f * expf((y+7.0f)*(6.0f/10.0f));
      // Maps -7 to 3 accel into 0 to 6.
      // Then e^(0~6) gives about 8.5 octaves range,
      // covering 20Hz to 8,069Hz, more than the full range of a piano.
      // Note that accel ranges about ±9, but the filter code will
      // correctly bound the range possible with the filter.
    filt.setFreqAndQ(f, 0.6);

    auto x = CircuitPlayground.motionX();
    float g = min(max(0.0f, x * (0.5f / -6.0f) + 0.5f), 1.0f);
    gate1.setPosition(g);
    gate2.setPosition(g);
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
      // Serial.print("tp1: "); tp1.printStats(Serial);
      // Serial.print("tp2: "); tp2.printStats(Serial);
      // Serial.println("----");
      DmaDac::report(Serial);
    }
  }
#endif
}
