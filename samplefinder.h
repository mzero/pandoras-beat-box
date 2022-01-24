#pragma once

#include "sound.h"
#include "types.h"


namespace SampleFinder {

  void setup(const char* suffix);

  void enter();
  void exit();

  void loop(millis_t);
  void display(millis_t);


  struct FlashSamples {
    Samples left;
    Samples right;
  };

  bool newFlashSamplesAvailable();
    // on setup, or when user loads new from SDFat
    // reset when when flashSamples() is called

  FlashSamples flashSamples();
}

