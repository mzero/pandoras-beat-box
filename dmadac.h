#pragma once

#include <stdint.h>
#include <Print.h>

#include "sound.h"

extern SoundSource& zeroSource;

namespace DmaDac {
  void begin();
  void setSource(SoundSource&);
  inline void clearSource() { setSource(zeroSource); }

  void report(Print& out);
}
