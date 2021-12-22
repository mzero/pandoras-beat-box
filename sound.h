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

