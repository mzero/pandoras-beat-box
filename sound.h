#pragma once

#include <FixedPoints.h>

using sample_t = SFixed<2, 13>;

constexpr sample_t SAMPLE_ZERO = sample_t(0);
constexpr sample_t SAMPLE_UNIT = sample_t(1);
constexpr sample_t SAMPLE_POS_ONE = sample_t(1.0);
constexpr sample_t SAMPLE_NEG_ONE = sample_t(-1.0);

constexpr float SAMPLE_RATE_TARGET = 48000.0;
constexpr long SAMPLE_RATE_CPU_DIVISOR = F_CPU / (long)SAMPLE_RATE_TARGET;
constexpr float SAMPLE_RATE = (float)F_CPU / (float)SAMPLE_RATE_CPU_DIVISOR;


class SoundSource {
public:
  virtual void supply(sample_t* buffer, int count) = 0;
    // NB: Will get called at interrupt time!
};


class TriangleToneSource : public SoundSource {
public:
  TriangleToneSource();

  void playNote(float freq, float dur);

  virtual void supply(sample_t* buffer, int count);

private:
  UFixed<0, 32> theta;  // current location in cycle
  UFixed<0, 32> delta;  // amount of theta per sample

  UFixed<0, 32> amp;
  UFixed<0, 32> decay;
};

class Samples {
public:
  Samples() : samples(nullptr), sampleCount(0) { }
  Samples(void* data, size_t len)
    : samples((sample_t*)data), sampleCount(len / sizeof(sample_t)) { }

  using sample_t = SFixed<0, 7>;

  sample_t operator[](int n) const { return samples[n]; }
  int      length()          const { return sampleCount; }

private:
  sample_t* samples;
  int sampleCount;

};

class SampleSourceBase : public SoundSource {
public:
  SampleSourceBase();
  void load(const Samples& s);
  void play(float amp);

protected:
  Samples samples;
  int nextSample;

  using comp_t = SFixed<15, 16>;
  comp_t amp;
};

template<int sample_rate>
class SampleSource : public SampleSourceBase {
public:
  SampleSource() { }
  virtual void supply(sample_t* buffer, int count);
};



class SampleGateSourceBase : public SoundSource {
public:
  SampleGateSourceBase();
  void load(const Samples& s) { samples = s; }

  template<typename T>
  void gate(T cv, T rangeMin, T rangeMax, T threshold);

  void setPosition(float);

protected:
  Samples samples;
  int startSample;
  int nextSample;

  using amp_t = UFixed<0, 32>;

  amp_t amp;
  amp_t ampTarget;
};

template<int sample_rate>
class SampleGateSource : public SampleGateSourceBase {
public:
  SampleGateSource() { }
  virtual void supply(sample_t* buffer, int count);
};



template<typename T>
void SampleGateSourceBase::gate(T cv, T rangeMin, T rangeMax, T threshold) {
  if (cv < threshold) {
    ampTarget = amp_t(0);
    return;
  }
  if (ampTarget == amp_t(0)) nextSample = startSample;

  if (cv < rangeMin) cv = rangeMin;
  if (cv > rangeMax) cv = rangeMax;

  constexpr amp_t ampMin(0.5f);
  constexpr amp_t ampMax(0.90f);
  constexpr amp_t ampRange = ampMax - ampMin;

  float cvf = float(cv - rangeMin) / float(rangeMax - rangeMin);
  ampTarget = ampMin + amp_t(cvf)*ampRange;
}


class MixSource : public SoundSource {
public:
  MixSource(SoundSource& s1, SoundSource& s2);

  virtual void supply(sample_t* buffer, int count);

private:
  SoundSource& s1;
  SoundSource& s2;
};


class FilterSource : public SoundSource {
public:
  FilterSource(SoundSource& in);
  void setFreqAndQ(float freq, float q);
  void retune(float freqFactor);

  virtual void supply(sample_t* buffer, int count);

private:
  SoundSource& in;

  sample_t f;
  sample_t fb;

  sample_t b0;
  sample_t b1;
};
