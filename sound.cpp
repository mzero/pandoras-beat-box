#include "sound.h"

#include "filemanager.h"


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

Samples::Samples() : sampleCount(0) { }

void Samples::load(const char* prefix) {
  auto fileSize = FileManager::sampleFileSize(prefix);
  if (fileSize == 0) {
    sampleCount = 0;
    return;
  }

  sampleCount = min(fileSize, sizeof(samples)) / sizeof(samples[0]);
  Serial.printf("will try to read %d samples\n", sampleCount);

  auto readSize = FileManager::sampleFileLoad(prefix, 0,
    samples, sampleCount * sizeof(samples[0]));

  sampleCount = readSize / sizeof(samples[0]);

#if BYTE_ORDER != LITTLE_ENDIAN
#error Need to write code to swap little endian samples to bigendian
#endif

  Serial.printf("did read %d samples\n", sampleCount);
}



SampleSource::SampleSource()
  : nextSample(samples.length())
  { }

void SampleSource::load(const char* prefix) {
  samples.load(prefix);
  nextSample = samples.length();
}

void SampleSource::play(float ampf) {
  amp = ampf;
  decay = 0.9994f; // FIXME: Figure this out!
  nextSample = 0;
}

void SampleSource::supply(sample_t* buffer, int count) {
  while (nextSample < samples.length() && count) {
    count -= 1;
    *buffer++ = sample_t(samples[nextSample++]);

    amp *= decay;
  }
  while (count--) {
    *buffer++ = SAMPLE_ZERO;
  }
}


SampleGateSource::SampleGateSource()
  : nextSample(0), amp(0), ampTarget(0)
  { }

void SampleGateSource::load(const char* prefix) {
  samples.load(prefix);
  nextSample = 0;
}

void SampleGateSource::supply(sample_t* buffer, int count) {
  if (samples.length() < 1) {
    while (count--) {
      *buffer++ = SAMPLE_ZERO;
    }
    return;
  }

  while (count--) {
    SFixed<15, 16> v(samples[nextSample++]);
    if (nextSample >= samples.length()) nextSample = 0;

    SFixed<15, 16> w(samples[nextSample]);
    SFixed<15, 16> a(amp);
    a /= 4;

    *buffer++ = sample_t((v + v + v + v) * a);
    *buffer++ = sample_t((v + v + v + w) * a);
    *buffer++ = sample_t((v + v + w + w) * a);
    *buffer++ = sample_t((v + w + w + w) * a);
    count -= 3;

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
  if (freq > 12000.0f) freq = 12000.0f;
  if (freq < 20.0f) freq = 20.0f;
  float fsr = freq/SAMPLE_RATE;
  f = fsr;
  fb = q + q/(1.0 - fsr);
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
