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

    static const sample_fixed_t scale(2 * SAMPLE_UNIT);
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


SampleSourceBase::SampleSourceBase()
  : nextSample(samples.length())
  { }

void SampleSourceBase::load(const Samples& s) {
  samples = s;
  nextSample = samples.length();
}

void SampleSourceBase::play(float ampf) {
  amp = ampf;
  nextSample = 0;
}

template<>
void SampleSource<int(SAMPLE_RATE)>::supply(sample_t* buffer, int count) {
  while (nextSample < samples.length() && count) {
    count -= 1;
    *buffer++ = sample_t(samples[nextSample++]*amp);
  }
  while (count--) {
    *buffer++ = SAMPLE_ZERO;
  }
}

template<>
void SampleSource<int(SAMPLE_RATE/2)>::supply(sample_t* buffer, int count) {
  comp_t a = amp/2;

  while (nextSample < samples.length() && count) {
    count -= 2;
    comp_t v = comp_t(samples[nextSample++]);
    comp_t w = nextSample < samples.length() ? comp_t(samples[nextSample]) : v;

    *buffer++ = sample_t((v + v) * a);
    *buffer++ = sample_t((v + w) * a);
  }
  while (count--) {
    *buffer++ = SAMPLE_ZERO;
  }
}

template<>
void SampleSource<int(SAMPLE_RATE/4)>::supply(sample_t* buffer, int count) {
  comp_t a = amp/4;

  while (nextSample < samples.length() && count) {
    count -= 4;
    comp_t v = comp_t(samples[nextSample++]);
    comp_t w = nextSample < samples.length() ? comp_t(samples[nextSample]) : v;

    *buffer++ = sample_t((v + v + v + v) * a);
    *buffer++ = sample_t((v + v + v + w) * a);
    *buffer++ = sample_t((v + v + w + w) * a);
    *buffer++ = sample_t((v + w + w + w) * a);
  }
  while (count--) {
    *buffer++ = SAMPLE_ZERO;
  }
}

SampleGateSourceBase::SampleGateSourceBase()
  : startSample(0), nextSample(0), amp(0), ampTarget(0)
  { }

void SampleGateSourceBase::setPosition(float p) {
  int l = samples.length();
  if (l < sampleRate() / 2) return;

  startSample = int(float(l)*p) % l;
}

// TODO: Write the SAMPLE_RATE version of SampleGateSource::supply()

template<>
void SampleGateSource<int(SAMPLE_RATE/2)>::supply(sample_t* buffer, int count) {
  if (samples.length() < 1) {
    while (count--) {
      *buffer++ = SAMPLE_ZERO;
    }
    return;
  }

  while (count) {
    SFixed<15, 16> v(samples[nextSample++]);
    if (nextSample >= samples.length()) nextSample = 0;

    SFixed<15, 16> w(samples[nextSample]);
    SFixed<15, 16> a(amp);
    a /= 2;

    *buffer++ = sample_t((v + v) * a);
    *buffer++ = sample_t((v + w) * a);
    count -= 2;

    /*
      slew = 1 - nth root (1 - 1/1db))
    */
    constexpr amp_t slewUp(0.074146f);     // 1.2ms
    constexpr amp_t slewDown(0.001087f);   // 85ms

    if (amp < ampTarget)      amp += (ampTarget - amp) * slewUp;
    else                      amp -= (amp - ampTarget) * slewDown;
  }
}

template<>
void SampleGateSource<int(SAMPLE_RATE/4)>::supply(sample_t* buffer, int count) {
  if (samples.length() < 1) {
    while (count--) {
      *buffer++ = SAMPLE_ZERO;
    }
    return;
  }

  while (count) {
    SFixed<15, 16> v(samples[nextSample++]);
    if (nextSample >= samples.length()) nextSample = 0;

    SFixed<15, 16> w(samples[nextSample]);
    SFixed<15, 16> a(amp);
    a /= 4;

    *buffer++ = sample_t((v + v + v + v) * a);
    *buffer++ = sample_t((v + v + v + w) * a);
    *buffer++ = sample_t((v + v + w + w) * a);
    *buffer++ = sample_t((v + w + w + w) * a);
    count -= 4;

    /*
      slew = 1 - nth root (1 - 1/1db))
    */
    constexpr amp_t slewUp(0.14279f);     // 1.2ms
    constexpr amp_t slewDown(0.00217f);   // 85ms

    if (amp < ampTarget)      amp += (ampTarget - amp) * slewUp;
    else                      amp -= (amp - ampTarget) * slewDown;
  }
}


MixSource::MixSource(SoundSource& _s1, SoundSource& _s2)
  : s1(_s1), s2(_s2)
  { }

void MixSource::supply(sample_t* buffer, int count) {
  s1.supply(buffer, count);

  sample_t* buf2 = (sample_t*)(alloca(sizeof(sample_t) * count));
  s2.supply(buf2, count);

  for (int i = 0; i < count; ++i) {
    sample_t s = (*buffer + *buf2++);
    *buffer++ = s;
  }
}

FilterSource::FilterSource(SoundSource& _in)
  : in(_in), b0(0), b1(1)
{
  setFreqAndQ(2540.0f, 0.2f);
}

void FilterSource::setFreqAndQ(float freq, float q)
{
  constexpr float freq_max = SAMPLE_RATE / 2.5f;
  freq = min(freq, freq_max);

  constexpr float c = PI / SAMPLE_RATE;
  constexpr bool accurate = true;

  if (accurate)
    f = 2.0f * sinf(c * freq);
  else {
    constexpr float d = 2.0f * c;
    f = d * freq;
    if (f > 2.0f) f = 2.0f;
  }

  fb = q + q/(1.0 - f);
}

void FilterSource::supply(sample_t* buffer, int count) {
  in.supply(buffer, count);

  while (count--) {
    sample_t s_in = *buffer;

    b0 = b0 + f * (s_in - b0 + fb * (b0 - b1));
    b1 = b1 + f * (b0 - b1);

    *buffer++ = b1;
  }
}
