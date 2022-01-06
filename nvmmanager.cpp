#include "nvmmanager.h"

extern uint32_t __etext;

namespace NvmManager {

  void* dataBegin() {
    const uint32_t program_end = (uint32_t)&__etext;
    const size_t program_size = 120 * 1024;
    uint32_t begin = FLASH_ADDR + program_size;

    if (begin < program_end) {
      begin = (program_end + 1023) & ~1023;

      Serial.printf("Program is larger than expected: %08x\n", program_end);
      Serial.printf("Adjusting file storage area to:  %08x (%dk)\n",
        begin, ((uint32_t)dataEnd() - begin + 1023)/1024);
    }

    return (void *)begin;
  }

  void* dataEnd() {
    return (void*)(FLASH_ADDR + FLASH_SIZE);
  }
}

namespace {
  inline void wait_nvm_ready() {
    while (NVMCTRL->INTFLAG.bit.READY == 0) ;
  }
}

namespace NvmManager {
  bool dataWrite(void* dst, void* src, size_t len) {
    // NB: dst must be aligned on ROW boundary

    uint32_t* wDst = (uint32_t*)dst;
    uint32_t* wSrc = (uint32_t*)src;
    size_t wLen = (len + sizeof(uint32_t) - 1) / sizeof(uint32_t);

    // Serial.printf("flahsing %08x for %5d words, from %08x\n",
    //   wDst, wLen, wSrc);

    NVMCTRL->CTRLB.bit.MANW = 1;
    while (wLen > 0) {
      wait_nvm_ready();
      NVMCTRL->STATUS.reg = NVMCTRL_STATUS_MASK;

      // Serial.printf("  erasing row at %08x\n", wDst);
      // Execute "ER" Erase Row
      NVMCTRL->ADDR.reg = (uint32_t)wDst / 2;
      NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_ER;
      wait_nvm_ready();

      // write one page at a time
      constexpr int pagesPerRow = NVMCTRL_ROW_SIZE / NVMCTRL_PAGE_SIZE;
      for (int i = 0; wLen > 0 && i < pagesPerRow; ++i) {
        //Serial.printf("  page %d: clearing ... ", i);
        // Execute "PBC" Page Buffer Clear
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
        wait_nvm_ready();

        size_t cnt = min(NVMCTRL_PAGE_SIZE / sizeof(uint32_t), wLen);
        wLen -= cnt;
        //Serial.printf("copying %d words ...", cnt);
        while (cnt--)
            *wDst++ = *wSrc++;

        //Serial.println("writing");
        // Execute "WP" Write Page
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;
        wait_nvm_ready();
      }

      if (NVMCTRL->STATUS.bit.NVME == 1) {
        Serial.println("programming error during flash");
        return false;
      }
      if (NVMCTRL->STATUS.bit.LOCKE == 1) {
        Serial.println("lock error during flash");
        return false;
      }
      if (NVMCTRL->STATUS.bit.PROGE == 1) {
        Serial.println("command error during flash");
        return false;
      }
    }

    return true;
  }

}
