#include "filemanager.h"

#include <SdFat.h>

#include "msg.h"
#include "nvmmanager.h"

namespace {
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
}

