#include "filemanager.h"

#include <delay.h>
#include <SPI.h>
#include <SdFat.h>

#include <Adafruit_SPIFlash.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_SleepyDog.h>

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

  uint32_t changeSettledAt = 0;

  void noteFileSystemChange() {
    changeSettledAt = millis() + 250;
  }


  char msgBuf[128];
  bool msgWaiting = false;

  void queueMsg(const char* msg) {
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
}

namespace FileManager {

  bool changed() {
    if (changeSettledAt > 0 && changeSettledAt < millis()) {
      changeSettledAt = 0;
      return true;
    }
    return false;
  }

  bool changing() {
    return changeSettledAt > 0;
  }
}


/* -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- */

namespace {

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
    noteFileSystemChange();
  }

  bool setupMSC() {
    usb_msc.setID("e.k", "pandora-beat-box", "1.0");
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

  bool setup() {
    if (!flash.begin()) {
      errorMsg("Failed to initialize flash chip.");
      return false;
    }
    flashTransport.setClockSpeed(4000000, 4000000);
      // flash is just flaky at higher speeds it seems

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

    noteFileSystemChange();

    if (!setupMSC()) {
      errorMsg("Failed to setup USB drive.");
      return false;
    }

    return true;
  }

  void loop() {
    showMsg();
  }
}

/* -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- */

namespace {
  bool matchSampleFileName(const char* prefix, FatFile& file) {
    char name[512];
    file.getName(name, sizeof(name));

    String nameStr(name);
    nameStr.toLowerCase();

    return
      nameStr.startsWith(prefix)
      && nameStr.endsWith(".raw");
  }

  void findSampleFile(const char* prefix, FatFile& sampleFile) {
    sampleFile.close();

    FatFile root;
    if (!root.open("/")) {
      errorMsg("open root failed");
      return;
    }

    FatFile file;
    while (file.openNext(&root, O_RDONLY)) {
      if (matchSampleFileName(prefix, file)) {
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

    if (!sampleFile.isOpen()) {
      errorMsgf("no %s*.raw file found", prefix);
    }
  }
}

namespace FileManager {
  uint32_t sampleFileSize(const char* prefix) {
    FatFile sampleFile;
    findSampleFile(prefix, sampleFile);
    uint32_t s = sampleFile.isOpen() ? sampleFile.fileSize() : 0;
    sampleFile.close();
    return s;
  }

  uint32_t sampleFileLoad(const char* prefix, uint32_t offset,
    void* buf, size_t bufSize)
  {
    FatFile sampleFile;
    findSampleFile(prefix, sampleFile);
    if (!sampleFile.isOpen()) return 0;

    sampleFile.seekSet(offset);
    uint32_t s = sampleFile.read(buf, bufSize);
    sampleFile.close();
    return s;
  }
}

