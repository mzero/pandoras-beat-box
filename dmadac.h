#pragma once

#include <stdint.h>
#include "Print.h"

using sample_t = uint16_t;

const int SAMPLE_BITS = 10;     // fixed in the SAM D21 architecture

const sample_t SAMPLE_PLUS_ONE  = 0x3ff;
const sample_t SAMPLE_ZERO      = 0x200;
const sample_t SAMPLE_NEG_ONE   = 0x001;

constexpr float SAMPLE_RATE_TARGET = 48000.0;
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
