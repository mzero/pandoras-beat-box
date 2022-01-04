#pragma once

#include <stddef.h>
#include <stdint.h>
#include "types.h"

namespace FileManager {
  struct SampleFiles {
    size_t    leftSize;
    void *    leftData;

    size_t    rightSize;
    void *    rightData;
  };

  bool locateFiles(SampleFiles&);
  void showMessages();
}


