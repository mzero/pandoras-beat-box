#include <Arduino.h>

namespace NvmManager {
  constexpr size_t block_size = NVMCTRL_ROW_SIZE;
    // writes must be both aligned to this, and a multiple of it

  void* dataBegin();  // at the start of the data area
  void* dataEnd();    // just beyond the end of the data area

  bool dataWrite(void* dst, void* src, size_t len);
    // this will erase the data area for len, rounded up to nearest block_size


  inline size_t blockRound(size_t x) {
    constexpr size_t b1 = NvmManager::block_size - 1;
    return (x + b1) & ~b1;
  }

  inline void* blockAfter(void* a, size_t x) {
    return (void*)((uint8_t*)a + blockRound(x));
  }
}
