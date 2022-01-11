#include "filemanager.h"

#include <delay.h>
#include <SPI.h>
#include <SdFat.h>

#include <Adafruit_SPIFlash.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_SleepyDog.h>

#include "nvmmanager.h"

namespace {

  // On-board external flash (QSPI or SPI) macros should already
  // defined in your board variant if supported
  // - EXTERNAL_FLASH_USE_QSPI
  // - EXTERNAL_FLASH_USE_CS/EXTERNAL_FLASH_USE_SPI
  #if defined(EXTERNAL_FLASH_USE_QSPI)
    Adafruit_FlashTransport_QSPI flashTransport;
  #elif defined(EXTERNAL_FLASH_USE_SPI)
    Adafruit_FlashTransport_SPI flashTransport(
      EXTERNAL_FLASH_USE_CS, EXTERNAL_FLASH_USE_SPI);
  #else
    #error No QSPI/SPI flash are defined on your board variant.h !
  #endif

  Adafruit_SPIFlash flash(&flashTransport);
  FatFileSystem fatfs;
  Adafruit_USBD_MSC usb_msc;


  char msgBuf[128];
  bool msgWaiting = false;
  bool holdMessages = true;

  void queueMsg(const char* msg) {
    if (!holdMessages) {
      Serial.println(msg);
      return;
    }
    if (msgWaiting) return;
    strncpy(msgBuf, msg, sizeof(msgBuf));
    msgBuf[sizeof(msgBuf)-1] = '\0';
    msgWaiting = true;
  }

  void showMsg() {
    if (msgWaiting) {
      Serial.println(msgBuf);
      msgWaiting = false;
    }
    holdMessages = false;
  }

  void statusMsg(const char* msg) {
    queueMsg(msg);
  }

  void errorMsg(const char* msg) {
    char buf[128];
    snprintf(buf, sizeof(buf), "** %s", msg);
    queueMsg(buf);
  }

  void statusMsgf(const char* fmt, ... ) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    statusMsg(buf);
  }

  void errorMsgf(const char* fmt, ... ) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    errorMsg(buf);
  }


/* -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- */

  int32_t msc_read_cb (uint32_t lba, void* buffer, uint32_t bufsize)
  {
    return flash.readBlocks(lba, (uint8_t*) buffer, bufsize/512) ? bufsize : -1;
  }

  int32_t msc_write_cb (uint32_t lba, uint8_t* buffer, uint32_t bufsize)
  {
    return flash.writeBlocks(lba, buffer, bufsize/512) ? bufsize : -1;
  }

  void msc_flush_cb (void)
  {
    flash.syncBlocks();
    fatfs.cacheClear();
  }

  bool setupMSC() {
    usb_msc.setID("e.k", "Pandora Beat Box", "1.0");
    usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
    usb_msc.setCapacity(flash.pageSize()*flash.numPages()/512, 512);
    usb_msc.setUnitReady(true);
    return usb_msc.begin();
  }

}

/* -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- */
// Formatting via elm-chan's fatfs code

namespace elm_chan_fatfs {

  #include "elm-chan/ff.c"
  // This pulls in the required elm-chan fatfs code. It is done like this to
  // keep it isolated as much as possible.

  extern "C"
  {

    DSTATUS disk_status ( BYTE pdrv )
    {
      (void) pdrv;
      return 0;
    }

    DSTATUS disk_initialize ( BYTE pdrv )
    {
      (void) pdrv;
      return 0;
    }

    DRESULT disk_read (
      BYTE pdrv,    /* Physical drive nmuber to identify the drive */
      BYTE *buff,   /* Data buffer to store read data */
      DWORD sector, /* Start sector in LBA */
      UINT count    /* Number of sectors to read */
    )
    {
      (void) pdrv;
      return flash.readBlocks(sector, buff, count) ? RES_OK : RES_ERROR;
    }

    DRESULT disk_write (
      BYTE pdrv,      /* Physical drive nmuber to identify the drive */
      const BYTE *buff, /* Data to be written */
      DWORD sector,   /* Start sector in LBA */
      UINT count      /* Number of sectors to write */
    )
    {
      (void) pdrv;
      return flash.writeBlocks(sector, buff, count) ? RES_OK : RES_ERROR;
    }

    DRESULT disk_ioctl (
      BYTE pdrv,    /* Physical drive nmuber (0..) */
      BYTE cmd,   /* Control code */
      void *buff    /* Buffer to send/receive control data */
    )
    {
      (void) pdrv;

      switch ( cmd )
      {
        case CTRL_SYNC:
          flash.syncBlocks();
          return RES_OK;

        case GET_SECTOR_COUNT:
          *((DWORD*) buff) = flash.size()/512;
          return RES_OK;

        case GET_SECTOR_SIZE:
          *((WORD*) buff) = 512;
          return RES_OK;

        case GET_BLOCK_SIZE:
          *((DWORD*) buff) = 8;    // erase block size in units of sector size
          return RES_OK;

        default:
          return RES_PARERR;
      }
    }

  }

  bool checkFR(const char* opstr, FRESULT r) {
    if (r != FR_OK) {
      errorMsgf("%s (err %d)", opstr, r);
      return true;
    }
    statusMsgf("%s good", opstr);
    return false;
  }

  bool format() {

    FATFS elmchamFatfs;
    uint8_t workbuf[4096]; // Working buffer for f_fdisk function.

    if (checkFR("mkfs",
        f_mkfs("", FM_FAT | FM_SFD, 0, workbuf, sizeof(workbuf))))
      return false;

    if (checkFR("mount", f_mount(&elmchamFatfs, "", 1)))   return false;
    if (checkFR("label", f_setlabel("MultiFlash")))        return false;
    if (checkFR("unmount", f_unmount("")))                 return false;

    return true;
  }
}

namespace {
  bool checkSD(const char* opstr, bool r) {
    if (!r)
      errorMsgf("error in %s", opstr);
    else
      statusMsgf("%s good", opstr);
    return !r;
  }

  bool touch(const char* path) {
    FatFile file;
    return file.open(fatfs.vwd(), path, FILE_WRITE);
  }
}

namespace FileManager {
  bool setupFileSystem() {
    if (!flash.begin()) {
      errorMsg("Failed to initialize flash chip.");
      return false;
    }

    if (!fatfs.begin(&flash)) {
      statusMsg("Formatting internal flash");

      if (!elm_chan_fatfs::format())
        return false;

      // sync to make sure all data is written to flash
      flash.syncBlocks();

      if (!fatfs.begin(&flash)) {
        statusMsg("Format failure");
        return false;
      }

      if (checkSD("mkdir", fatfs.mkdir("/.fseventsd")))         return false;
      if (checkSD("touch 1", touch("/.fseventsd/no_log")))      return false;
      if (checkSD("touch 2", touch("/.metadata_never_index")))  return false;
      if (checkSD("touch 3", touch("/.Trashes")))               return false;

      // sync to make sure all data is written to flash
      flash.syncBlocks();

      statusMsg("Done, resetting....");
      Watchdog.enable(2000);
      delay(3000);
    }

    if (!setupMSC()) {
      errorMsg("Failed to setup USB drive.");
      return false;
    }

    return true;
  }
}

namespace {
/* -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- */

  bool matchSampleFileName(
    const char* prefix, const char* suffix, FatFile& file)
  {
    char name[512];
    file.getName(name, sizeof(name));

    String nameStr(name);
    nameStr.toLowerCase();

    return
      nameStr.startsWith(prefix)
      && nameStr.endsWith(suffix);
  }

  void findSampleFile(
    const char* prefix, const char* suffix, FatFile& sampleFile)
  {
    sampleFile.close();

    FatFile root;
    if (!root.open("/")) {
      errorMsg("open root failed");
      return;
    }

    FatFile file;
    while (file.openNext(&root, O_RDONLY)) {
      if (matchSampleFileName(prefix, suffix, file)) {
        if (!sampleFile.isOpen()) {
          sampleFile = file;
        } else {
          errorMsgf("multiple %s*.raw files found", prefix);
        }
      }
      file.close();
    }

    if (root.getError()) {
      errorMsg("root openNext failed");
    }
    root.close();
  }


  struct FlashedFileInfo {
    void* data;
    size_t size;

    uint16_t modTime;
    uint16_t modDate;
  };

  struct FlashedDir {
    uint32_t magic;
    static const uint32_t magic_marker = 0x69A57FDA;

    FlashedFileInfo leftFile;
    FlashedFileInfo rightFile;
  };

  bool checkFile(const char* prefix, const char* suffix,
    FatFile& file, bool& flash, FlashedFileInfo& info) {

    findSampleFile(prefix, suffix, file);
    if (!file.isOpen()) {
      statusMsgf("no %s file found", prefix);
      // no left file found -- okay
      if (info.size > 0) {
        info.size = 0;
        flash = true;
      }
      return true;
    }

    size_t s = file.fileSize();

    dir_t d;
    if (!file.dirEntry(&d)) {
      errorMsgf("dirEntry on %s file failed", prefix);
      return false;
    }

    statusMsgf("%s file %d bytes, %04x:%04x mod",
      prefix, s, d.lastWriteDate, d.lastWriteTime);

    if (s == info.size
    && d.lastWriteTime == info.modTime
    && d.lastWriteDate == info.modDate) {
      return true;
    }

    info.size = s;
    info.modTime = d.lastWriteTime;
    info.modDate = d.lastWriteDate;
    flash = true;
    return true;
  }

  inline size_t blockRound(size_t x) {
    const size_t b1 = NvmManager::block_size - 1;
    return (x + b1) & ~b1;
  }

  inline void* blockAfter(void* a, size_t x) {
    return (void*)((uint8_t*)a + blockRound(x));
  }

  bool flashFromFile(FatFile& file, FlashedFileInfo& info) {
    uint8_t buffer[NvmManager::block_size];

    size_t s = info.size;
    void* dst = info.data;

    while (s > 0) {
      auto r = file.read(buffer, min(s, NvmManager::block_size));
      if (r <= 0) {
        errorMsg("error reading file");
        return false;
      }
      if (!NvmManager::dataWrite(dst, buffer, r)) {
        errorMsg("error flashing file");
        return false;
      }
      dst = ((uint8_t*)dst) + NvmManager::block_size;
      s -= r;
    }

    return true;
  }
}

namespace FileManager {

  bool locateFiles(SampleFiles& sf, const char* suffix) {
    void* dataBegin = NvmManager::dataBegin();
    void* fileBegin = blockAfter(dataBegin, sizeof(FlashedDir));
    void* dataEnd = NvmManager::dataEnd();

    FlashedDir fd = *(FlashedDir*)dataBegin;

    bool flashLeft = false;
    bool flashRight = false;
    bool flashDir = false;

    if (fd.magic != FlashedDir::magic_marker) {
      statusMsg("initing dir");
      fd.magic = FlashedDir::magic_marker;
      fd.leftFile.data = fileBegin;
      fd.leftFile.size = 0;
      fd.rightFile.data = fileBegin;
      fd.rightFile.size = 0;

      flashDir = true;
    }

    FatFile leftFile;
    FatFile rightFile;
    if (!checkFile("left",  suffix, leftFile,  flashLeft,  fd.leftFile))
      return false;
    if (!checkFile("right", suffix, rightFile, flashRight, fd.rightFile))
      return false;

    void* leftEndrightStart = blockAfter(fileBegin, fd.leftFile.size);
    void* rightEnd = blockAfter(leftEndrightStart, fd.rightFile.size);
    if (rightEnd >= dataEnd) {
      errorMsgf("left and right files are too large to flash: %dk > %dk",
        ((uint32_t)rightEnd - (uint32_t)fileBegin)/1024,
        ((uint32_t)dataEnd - (uint32_t)fileBegin)/1024);
      return false;
    }

    if (flashLeft) {
      fd.leftFile.data = fileBegin;
      statusMsgf("flashing left file to %08x for %d bytes",
        fd.leftFile.data, fd.leftFile.size);
      if (!flashFromFile(leftFile, fd.leftFile)) {
        errorMsg("error flashing left file");
        return false;
      }
    }
    if (flashRight) {
      fd.rightFile.data = (void*)((uint8_t*)dataEnd - blockRound(fd.rightFile.size));
      statusMsgf("flashing right file to %08x for %d bytes",
        fd.rightFile.data, fd.rightFile.size);
      if (!flashFromFile(rightFile, fd.rightFile)) {
        errorMsg("error flashing right file");
        return false;
      }
    }
    if (flashLeft || flashRight || flashDir) {
      statusMsg("flashing dir");
      if (!NvmManager::dataWrite(dataBegin, (void*)&fd, sizeof(FlashedDir))) {
        errorMsg("error flashing directory");
        return false;
      }
    }

    statusMsg("using flashed files:");
    statusMsgf("   left at %08x for %8d bytes", fd.leftFile.data, fd.leftFile.size);
    statusMsgf("  right at %08x for %8d bytes", fd.rightFile.data, fd.rightFile.size);
    sf.leftData = fd.leftFile.data;
    sf.leftSize = fd.leftFile.size;
    sf.rightData = fd.rightFile.data;
    sf.rightSize = fd.rightFile.size;

    return true;
  }

  void showMessages() {
    showMsg();
  }
}

