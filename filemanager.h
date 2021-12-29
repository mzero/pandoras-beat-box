#pragma once

#include <stddef.h>
#include <stdint.h>
#include "types.h"

namespace FileManager {
  bool setup();
  void loop();

  bool filesUpdated();
  uint32_t sampleFileSize(const char* prefix);
  uint32_t sampleFileLoad(const char* prefix, uint32_t offset,
    void* buf, size_t bufSize);
    // FIX THIS API
}
