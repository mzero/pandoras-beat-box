#pragma once

#include <FixedPoints.h>

#include "dmadac.h"


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
  Samples();
  void load(const char* prefix);

  using sample_t = SFixed<0, 7>;

  sample_t operator[](int n) const { return samples[n]; }
  int      length()          const { return sampleCount; }

private:
  static const int maxSampleCount = 12000;

  sample_t samples[maxSampleCount];
  int sampleCount;
};

class SampleSource : public SoundSource {
public:
  SampleSource();
  void load(const char* prefix);
  void play(float amp);

  virtual void supply(sample_t* buffer, int count);

private:
  Samples samples;

  int nextSample;
  UFixed<0, 32> amp;
  UFixed<0, 32> decay;
};
