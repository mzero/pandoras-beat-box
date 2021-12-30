#pragma once

#include <stdint.h>
#include "Print.h"

using sample_t = uint16_t;

const int SAMPLE_BITS = 10;           // DAC on SAM D21 is only 10 bits
const sample_t SAMPLE_ZERO = 1 << (SAMPLE_BITS - 1);
const sample_t SAMPLE_UNIT = SAMPLE_ZERO - 1;
const sample_t SAMPLE_POS_ONE = SAMPLE_ZERO + SAMPLE_UNIT;
const sample_t SAMPLE_NEG_ONE = SAMPLE_ZERO - SAMPLE_UNIT;

constexpr float SAMPLE_RATE_TARGET = 12000.0;
constexpr long SAMPLE_RATE_CPU_DIVISOR = F_CPU / (long)SAMPLE_RATE_TARGET;
constexpr float SAMPLE_RATE = (float)F_CPU / (float)SAMPLE_RATE_CPU_DIVISOR;

class SoundSource {
public:
  virtual void supply(sample_t* buffer, int count) = 0;
    // NB: Will get called at interrupt time!
};

extern SoundSource& zeroSource;

namespace DmaDac {
  void begin();
  void setSource(SoundSource&);
  inline void clearSource() { setSource(zeroSource); }

  void report(Print& out);
}
