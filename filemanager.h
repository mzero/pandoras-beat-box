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

  bool setupFileSystem();
  bool locateFiles(SampleFiles&, const char* suffix);
}


