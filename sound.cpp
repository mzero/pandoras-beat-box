#include "sound.h"

TriangleToneSource::TriangleToneSource()
  : theta(0), delta(0), amp(0), decay(0)
  { }

void TriangleToneSource::playNote(float freq, float dur) {
  delta = freq / SAMPLE_RATE;

  static const UFixed<0, 32> quarter = 0.25;
  theta = quarter;

  amp = 0.9;
  decay = 0.9f * dur / (float)SAMPLE_RATE;
}

void TriangleToneSource::supply(sample_t* buffer, int count) {
  using sample_fixed_t = SFixed<15, 16>;

  while (count--) {
    sample_fixed_t s(theta);

    static const sample_fixed_t half(0.5);
    if (s >= half) s = 1 - s;

    static const sample_fixed_t scale(2 * (SAMPLE_PLUS_ONE - SAMPLE_ZERO));
    s = s * scale;

    sample_fixed_t a(amp);
    s = s * a;

    static const sample_fixed_t zero_offset(SAMPLE_ZERO);
    s += zero_offset;

    *buffer++ = (sample_t)(s.getInteger());

    theta += delta;
    amp = amp > decay ? amp - decay : 0;
  }
}

