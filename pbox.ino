#include <Adafruit_CircuitPlayground.h>

using millis_t = unsigned long;
using micros_t = unsigned long;

const bool plot_touch = false;


class TouchPad {

public:
  TouchPad(int pin);
  void begin(millis_t);
  void loop(millis_t);

  static constexpr float capture_rate = 100.0; // per second
  static const millis_t capture_period = 1000.0 / capture_rate;
  static const millis_t warmup_period = 5000;
  static const millis_t sample_period = 50;

  using value_t = u_int16_t;

  value_t value()     const { return _value; }
  value_t min()       const { return _min; }
  value_t max()       const { return _max; }
  value_t threshold() const { return _threshold; }
  bool    warmed()    const { return _warmed; }

  void setThreshold();
  void printStats(Print&);

private:
  Adafruit_CPlay_FreeTouch cap;
    // FIXME: support using Adafruit_FreeTouch on non CPE boards
    // FIXME: support using CPlay_CapacitiveSensor on other CP baords

  millis_t _next_sample_time;
  millis_t _warmup_time;

  static const int sample_count = sample_period / capture_period;
  value_t _samples[sample_count];
  int _next_sample;

  value_t _value;
  value_t _min;
  value_t _max;
  value_t _threshold;
  bool    _warmed;
};

TouchPad::TouchPad(int pin)
  : cap(pin, OVERSAMPLE_1, RESISTOR_100K, FREQ_MODE_NONE)
  { }

void TouchPad::begin(millis_t now) {
  cap.begin();

  auto v0 = cap.measure();
  _next_sample_time = now + capture_period;
  _warmup_time = now + warmup_period;

  for (int i = 0; i < sample_count; ++i)
    _samples[i] = v0;
  _next_sample = 0;

  _value = v0;
  _min = v0;
  _max = v0;
  _threshold = v0;
  _warmed = false;

  if (plot_touch)
    Serial.print("min\tmax\tvalue\n");
}

void TouchPad::loop(millis_t now) {
  if (now >= _next_sample_time) {
    _next_sample_time += capture_period;
    if (_next_sample_time < now)
      _next_sample_time = now + capture_period;

    auto v = cap.measure();
    if (millis() >= _next_sample_time) v = 9;

    auto vOld = _samples[_next_sample];
    _samples[_next_sample] = v;
    _next_sample = (_next_sample + 1) % sample_count;

    _value = v;
    _min = ::min(_min, v);
    _max = ::max(_max, v);

    if (vOld <= _min || _max <= vOld) {
      // an extremis expired, so recompute the extremes
      _min = _max = _samples[0];
      for (int i = 1; i < sample_count; ++i) {
        auto s = _samples[i];
        _min = ::min(_min, s);
        _max = ::max(_max, s);
      }
    }

    if (plot_touch)
      Serial.printf("%d\t%d\t%d\n", _min, _max, v);
  }

  if (!_warmed && now >= _warmup_time) {
    setThreshold();
    _warmed = true;
  }
}

void TouchPad::setThreshold() {
  _threshold = _max + _max / 4;
}

void TouchPad::printStats(Print& out) {
  static bool first = false;
  if (!first) {
    first = true;
    out.printf("TouchPad config: capture %5dms, warmup %5dms, sample %5dms, count %3d\n",
      capture_period, warmup_period, sample_period, sample_count);
  }

  value_t mid = (_max + _min) / 2;
  value_t q = (_max - _min) / 8;
  value_t up_v = mid + q;
  value_t down_v = mid - q;

  bool up = _samples[_next_sample] >= up_v;
  int count = up ? 1 : 0;
  for (int i = 1; i < sample_count; ++i) {
    value_t v = _samples[(_next_sample + i) % sample_count];
    if (up) {
      if (v <= down_v) up = false;
    }
    else {
      if (v >= up_v) up = true, ++count;
    }
  }

  float hz = (float)count / ((float)sample_period / 1000.0f);
  int hz_units = (int)hz;
  hz = (hz - (float)hz_units) * 10.0f;
  int hz_tenths = (int)(hz + 0.5f);

  if (_warmed) {
    out.printf("TouchPad stats: range [%5d,%5d], thresh %5d, osc %2d.%01dHz\n",
      _min, _max, _threshold, hz_units, hz_tenths);
  }
  else {
    out.printf("TouchPad stats: range [%5d,%5d], thresh -----, osc %2d.%01dHz\n",
      _min, _max, hz_units, hz_tenths);
  }
}



TouchPad tp1 = TouchPad(A1);


void setup() {
  // Initialize serial port and circuit playground library.
  CircuitPlayground.begin();

  Serial.begin(115200);
  while (!CircuitPlayground.slideSwitch() && !Serial);
  if (!plot_touch)
    Serial.println("pandora's box");

  auto now = millis();
  tp1.begin(now);
}

const int readingsCount = 10;
uint16_t readings[readingsCount];
int readingsNext = 0;


void loop() {
  auto now = millis();

  tp1.loop(now);

  static millis_t neopix_update = 0;
  if (now >= neopix_update) {
    neopix_update = now + 100;

    CircuitPlayground.strip.setBrightness(20);

    for (int i=0; i<10; ++i) {
      TouchPad::value_t v0 = (i + 0) * 120;
      TouchPad::value_t v1 = (i + 1) * 120;
      TouchPad::value_t v = tp1.max(); // tp1.value();

      auto c = CircuitPlayground.colorWheel(256l*v/20l);
      static auto c_off = CircuitPlayground.strip.Color(0, 0, 0);
      static auto c_low = CircuitPlayground.strip.Color(30, 30, 30);
      static auto c_high = CircuitPlayground.strip.Color(255, 255, 255);

      if (v1 <= tp1.min() || tp1.max() < v0)
        c = c_off;
      if (v0 <= v && v < v1)
        c = (v < tp1.threshold()) ? c : c_high;

      CircuitPlayground.strip.setPixelColor(i, c);
    }
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