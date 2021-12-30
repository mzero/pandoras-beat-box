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

class SampleSource : public SoundSource {
public:
  SampleSource();
  void load(const char* prefix);
  void play(float amp);

  virtual void supply(sample_t* buffer, int count);

private:
  static const int maxSampleCount = 12000;

  SFixed<0, 7> samples[maxSampleCount];
  int sampleCount;

  int nextSample;
  UFixed<0, 32> amp;
  UFixed<0, 32> decay;
};
