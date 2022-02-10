#include "filesystem.h"

#include <delay.h>
#include <SPI.h>
#include <SdFat.h>

#include <Adafruit_SPIFlash.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_SleepyDog.h>

#include "msg.h"

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

    if (checkFR("mount", f_mount(&elmchamFatfs, "", 1)))  return false;
    if (checkFR("label", f_setlabel("BeatBox")))          return false;
    if (checkFR("unmount", f_unmount("")))                return false;

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

bool setupFileSystem() {
  if (!flash.begin()) {
    errorMsg("Failed to initialize flash chip.");
    return false;
  }

  if (!fatfs.begin(&flash)) {
    errorMsg("File system needs initialization.");
    errorMsg("Flash with the pbb-fs-init program.");
    return false;
  }

  if (!setupMSC()) {
    errorMsg("Failed to setup USB drive.");
    return false;
  }

  return true;
}

bool initFileSystem(bool force) {
  if (!flash.begin()) {
    errorMsg("Failed to initialize flash chip.");
    return false;
  }

  if (!force) {
    if (fatfs.begin(&flash)) {
      statusMsg("Already formatted, it seems.");
      return true;
    }
  }

  statusMsg("Formatting internal flash");
  if (!elm_chan_fatfs::format()) {
    errorMsg("Format return failure");
    return false;
  }

  // sync to make sure all data is written to flash
  flash.syncBlocks();

  if (!fatfs.begin(&flash)) {
  errorMsg("Format did not work");
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
