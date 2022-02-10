// NB: For best timinigs, compile whole program with -O2
//    Annotating individual functions with -O3 or higher, makes them slower!

#include <Adafruit_CircuitPlayground.h>

#include "dmadac.h"
#include "filesystem.h"
#include "msg.h"
#include "samplefinder.h"
#include "sound.h"
#include "touch.h"
#include "types.h"

const char* fileSuffix = "24k8.raw";
const int file_sample_rate = 24000;

TouchPad tp1 = TouchPad(A1);
TouchPad tp2 = TouchPad(A2);
int touchedOutPin = 0; // labeled "RX A6" on the board

SampleGateSource<file_sample_rate> gate1;
SampleGateSource<file_sample_rate> gate2;
MixSource mix(gate1, gate2);
FilterSource filt(mix);
DelaySource delayPedal(filt);
SoundSource& chainOut = delayPedal;


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
    CircuitPlayground.strip.setPixelColor(i, (9-i) <= v ? c_high : c_off);
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
      gate1.gate(0.9f);

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

bool testToneLoop(millis_t now) {
  static bool playingTestTone = false;
  bool playTestTone = CircuitPlayground.rightButton();

  if (playingTestTone != playTestTone) {
    DmaDac::setSource(playTestTone ? testRampSource : chainOut);
    playingTestTone = playTestTone;
  }

  return playingTestTone;
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

  bool fsSetup = false;
  {
    MessageHold msgHold;

    fsSetup = setupFileSystem();
    Serial.begin(115200);
    waitForSerial();
    Serial.println();
    Serial.println("> : ~ : .. : Pandora's Drumming Box : .. : ~ : <");
    Serial.println();
    Serial.flush();
  }

  if (fsSetup) {
    Serial.println("File system is setup and happy!");
  }

  // FIXME: what to do if fsSetup is false?
  SampleFinder::setup(fileSuffix);

  SampleFinder::FlashSamples fs = SampleFinder::flashSamples();
  gate1.load(fs.left);
  gate2.load(fs.right);

  auto now = millis();

  DmaDac::begin();
  DmaDac::setSource(chainOut);

  pinMode(touchedOutPin, OUTPUT);

  tp1.begin(now);
  tp2.begin(now);

  auto s1 = sramUsed();
  Serial.printf("sram used: %d static, %d post-init\n", s0, s1);
}


void loop() {
  auto now = millis();

  tp1.loop(now);
  tp2.loop(now);

  bool playable = true;

  static bool finderMode = false;

  if (CircuitPlayground.slideSwitch()) {
    // normal mode
    if (finderMode) {
      SampleFinder::exit();
      finderMode = false;
    }

    if (CircuitPlayground.leftButton()) {
      tp1.calibrate();
      tp1.calibrate();
    }
    // if (sweepLoop(now)) playable = false;
    // if (testToneLoop(now)) playable = false;
  } else {
    // Sample Finder Mode
    if (!finderMode) {
      SampleFinder::enter();
      finderMode = true;
    }

    SampleFinder::loop(now);
    if (SampleFinder::newFlashSamplesAvailable()) {
      SampleFinder::FlashSamples fs = SampleFinder::flashSamples();
      gate1.load(fs.left);
      gate2.load(fs.right);
    }
  }


  if (playable) {
    bool touched = false;
    if (tp1.calibrated()) {
      if (tp1.max() < tp1.threshold()) {
        gate1.gateOff();
      }
      else {
        touched = true;
        gate1.gate(map_range_clamped(float(tp1.max()),
          200.0f, 800.0f,   // touch range
          0.5f, 0.9f));     // amp range
      }
    }
    if (tp2.calibrated()) {
      if (tp2.max() < tp2.threshold()) {
        gate2.gateOff();
      }
      else {
        touched = true;
        gate2.gate(map_range_clamped(float(tp2.max()),
          200.0f, 800.0f,   // touch range
          0.5f, 0.9f));     // amp range
      }
    }
    digitalWrite(touchedOutPin, touched);

    static millis_t accel_update = 0;
    if (now >= accel_update) {
      accel_update = now + 100;

      sensors_event_t event;
      CircuitPlayground.lis.getEvent(&event);

      static float x, y, z;
        // these are static because they are filtered versions of the event
        // since the accellerometer values can be jumpy with quick user motions

      constexpr float accel_slew(0.63095734448);  // -20dB in 500ms
      x = event.acceleration.x - (event.acceleration.x - x) * accel_slew;
      y = event.acceleration.y - (event.acceleration.y - y) * accel_slew;
      z = event.acceleration.z - (event.acceleration.z - z) * accel_slew;

      float f = 30.0f * expf(map_range(y, -9.0f, 3.5f, 0.0f, 5.0f));
        // Maps -9 to 3.5 accel into 0 to 5.
        // Then e^(0~5) gives about 7 octaves range,
        // covering 30Hz to 4,452Hz.
        // Note that accel ranges about ±9, but the filter code will
        // correctly bound the range possible with the filter.
      filt.setFreqAndQ(f, 0.6f);

      float g = map_range_clamped(x, -4.0f, 4.0f, 0.0f, 1.0f);
      gate1.setPosition(g);
      gate2.setPosition(g);

      delayPedal.setDelayMod(map_range(x, 8.0f, -8.0f,
          DelaySource::minMod, DelaySource::maxMod));

      float k = 9.0f - z;
      k = 324.0f - k * k;
      delayPedal.setFeedback(map_range_clamped(k, 0.0f, 324.0f, 0.0f, 0.980f));

    }
  }

  static millis_t neopix_update = 0;
  if (now >= neopix_update) {
    neopix_update = now + 100;

    CircuitPlayground.strip.clear();

    if (!tp1.calibrated())        displayCalibration(now);
    else if (finderMode)          SampleFinder::display(now);
    else                          displayTouch(now);

    CircuitPlayground.strip.show();
  }

#if 0
    static millis_t stats_update = 0;
    if (now >= stats_update) {
      stats_update = now + 1000;
      // Serial.print("tp1: "); tp1.printStats(Serial);
      // Serial.print("tp2: "); tp2.printStats(Serial);
      // Serial.println("----");
      DmaDac::report(Serial);
  }
#endif
}
