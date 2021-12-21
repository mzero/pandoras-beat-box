#include "touch.h"
#include "types.h"

TouchPad::TouchPad(int pin)
  : cap(pin, OVERSAMPLE_1, RESISTOR_100K, FREQ_MODE_NONE)
  { }

void TouchPad::begin(millis_t now) {
  cap.begin();

  auto v0 = cap.measure();
  _next_sample_time = now + capture_period;
  _calibration_time = now + calibration_period;

  for (int i = 0; i < sample_count; ++i)
    _samples[i] = v0;
  _next_sample = 0;

  _value = v0;
  _min = v0;
  _max = v0;
  _threshold = v0;
  _calibrated = false;

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

  if (!_calibrated && now >= _calibration_time) {
    setThreshold();
    _calibrated = true;
  }
}

void TouchPad::calibrate() {
  _calibration_time = millis() + calibration_period;
  _calibrated = false;
}

millis_t TouchPad::calibrationTimeLeft(millis_t now) const {
  return _calibration_time <= now ? 0 : _calibration_time - now;
}

void TouchPad::setThreshold() {
  _threshold = _max + _max / 4;
}

void TouchPad::printStats(Print& out) {
  static bool first = false;
  if (!first) {
    first = true;
    out.printf("TouchPad config: capture %5dms, calibration %5dms, sample %5dms, count %3d\n",
      capture_period, calibration_period, sample_period, sample_count);
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

  if (_calibrated) {
    out.printf("TouchPad stats: range [%5d,%5d], thresh %5d, osc %2d.%01dHz\n",
      _min, _max, _threshold, hz_units, hz_tenths);
  }
  else {
    out.printf("TouchPad stats: range [%5d,%5d], thresh -----, osc %2d.%01dHz\n",
      _min, _max, hz_units, hz_tenths);
  }
}
