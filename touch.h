#pragma once

#include <utility/Adafruit_CPlay_FreeTouch.h>

#include "types.h"


class TouchPad {

public:
  TouchPad(int pin);
  void begin(millis_t);
  void loop(millis_t);

  static constexpr float capture_rate = 50.0; // per second
  static const millis_t capture_period = 1000.0 / capture_rate;
  static const millis_t sample_period = 50;
  static const millis_t calibration_period = 5000;

  using value_t = u_int16_t;

  value_t value()       const { return _value; }
  value_t min()         const { return _min; }
  value_t max()         const { return _max; }
  value_t threshold()   const { return _threshold; }
  bool    calibrated()  const { return _calibrated; }

  millis_t calibrationTimeLeft(millis_t now) const;

  void calibrate();
  void printStats(Print&);

private:
  Adafruit_CPlay_FreeTouch cap;
    // FIXME: support using Adafruit_FreeTouch on non CPE boards
    // FIXME: support using CPlay_CapacitiveSensor on other CP baords

  millis_t _next_sample_time;
  millis_t _calibration_time;

  static const int sample_count = sample_period / capture_period;
  value_t _samples[sample_count];
  int _next_sample;

  value_t _value;
  value_t _min;
  value_t _max;
  value_t _threshold;
  bool    _calibrated;

  void setThreshold();
};

