#include "samplefinder.h"

#include <array>

#include <Adafruit_CircuitPlayground.h>
#include <SdFat.h>

#include "msg.h"
#include "nvmmanager.h"

namespace {

  /***
   *** Sample Files stored in on-chip Flash
   ***/

  struct FlashedFile {
    void* data;
    size_t size;

    uint16_t modTime;
    uint16_t modDate;
  };

  struct FlashedDir {
    uint32_t magic;
    static const uint32_t magic_marker = 0x69A57FDA;

    FlashedFile left;
    FlashedFile right;
  };

  FlashedDir flashedDir;
  bool flashDirChanged = false;

  void loadFlashedSamples() {
    void* dataBegin = NvmManager::dataBegin();

    flashedDir = *(FlashedDir*)dataBegin;
    if (flashedDir.magic != FlashedDir::magic_marker) {
      flashedDir.magic = FlashedDir::magic_marker;
      flashedDir.left.data = nullptr;
      flashedDir.left.size = 0;
      flashedDir.right.data = nullptr;
      flashedDir.right.size = 0;
    }

    flashDirChanged = true;
  }

  /***
   *** Sample Files found on the SPI flash FAT file system
   ***/

  struct FileSamples {
    FatFile     file;
    size_t      size() const { return file.isOpen() ? file.fileSize() : 0; }
    uint16_t    modTime;
    uint16_t    modDate;

    bool        found() const { return file.isOpen(); }

    void setFile(FatFile& f);
    void reset() { file.close(); }
  };

  void FileSamples::setFile(FatFile& f) {
    file = f;
    if (!f.isOpen()) return;

    dir_t d;
    if (!file.dirEntry(&d)) {
      file.close();
      return;
    }

    modTime = d.lastWriteTime;
    modDate = d.lastWriteDate;
  }

  struct FilePair {
    FileSamples left;
    FileSamples right;

    enum {
      fp_notFound,
      fp_found,
      fp_loaded,
      fp_tooBig
    } status;

    void reset() { left.reset(); right.reset(); status = fp_notFound; }
  };

  bool flashMatchesFile(const FlashedFile& f, const FileSamples& s) {
    if (f.size == 0) {
      // nothing flashed...
      return !s.found() || s.size() == 0;
    }

    return s.found()
      && f.size == s.size()
      && f.modTime == s.modTime
      && f.modDate == s.modDate;
  }

  bool flashMatchesPair(const FilePair& p) {
    return flashMatchesFile(flashedDir.left, p.left)
      &&   flashMatchesFile(flashedDir.right, p.right);
  }

  bool flashCanHoldPair(const FilePair& p) {
    size_t size_left = p.left.found() ? p.left.size() : 0;
    size_t size_right = p.right.found() ? p.right.size() : 0;

    void* dataBegin = NvmManager::dataBegin();
    void* dataEnd = NvmManager::dataEnd();
    void* leftBegin = NvmManager::blockAfter(dataBegin, sizeof(FlashedDir));
    void* leftEndrightStart = NvmManager::blockAfter(leftBegin, size_left);
    void* rightEnd = NvmManager::blockAfter(leftEndrightStart, size_right);
    return rightEnd <= dataEnd;
  }

  void loadFileToFlash(void* data, FlashedFile& f, FileSamples& s) {
    f.data = data;
    f.size = s.size();
    f.modTime = s.modTime;
    f.modDate = s.modDate;

    uint8_t buffer[NvmManager::block_size];

    size_t count = f.size;
    void* dst = f.data;
    statusMsgf("flashing file to %08x for %d bytes", dst, count);

    while (count > 0) {
      auto r = s.file.read(buffer, min(count, NvmManager::block_size));
      if (r <= 0) {
        errorMsg("error reading file");
      }
      if (!NvmManager::dataWrite(dst, buffer, r)) {
        errorMsg("error flashing file");
      }
      dst = ((uint8_t*)dst) + NvmManager::block_size;
      count -= r;
    }
  }

  void loadPairToFlash(FilePair& p) {
    void* dataBegin = NvmManager::dataBegin();
    void* dataEnd = NvmManager::dataEnd();
    void* leftBegin = NvmManager::blockAfter(dataBegin, sizeof(FlashedDir));
    void* rightBegin = NvmManager::blockAfter(leftBegin, p.left.size());
    void* samplesEnd = NvmManager::blockAfter(rightBegin, p.right.size());

    loadFileToFlash(leftBegin, flashedDir.left, p.left);
    loadFileToFlash(rightBegin, flashedDir.right, p.right);
    if (!NvmManager::dataWrite(dataBegin, (void*)&flashedDir, sizeof(FlashedDir))) {
      errorMsg("error flashing directory");
    }

    flashDirChanged = true;
  }

  /***
   *** Pairs of Sample Files that could be loaded
   ***/

  std::array<FilePair, 5> pairs;

  void findFilePairs(const char* suffix) {
    for (auto& p : pairs) p.reset();

    FatFile root;
    if (!root.open("/")) {
      errorMsg("open root failed");
      return;
    }

    FatFile file;
    while (file.openNext(&root, O_RDONLY)) {

      char name[128];
      file.getName(name, sizeof(name));

      String nameStr(name);
      nameStr.toLowerCase();

      int d;

      if (!nameStr.endsWith(suffix)) goto nextFile;

      d = int(nameStr[0]) - int('1');
      if (d < 0 || pairs.size() <= d) goto nextFile;

      switch (nameStr[1]) {
        case 'l':   pairs[d].left.setFile(file);    break;
        case 'r':   pairs[d].right.setFile(file);   break;
        default:    goto nextFile;
      }

      statusMsgf("found file %s", name);

    nextFile:
      file.close();
    }

    if (root.getError()) {
      errorMsg("root openNext failed");
    }
    root.close();

    for (auto& p : pairs) {
      if (p.left.found() || p.right.found()) {
        // as long as we found one...
        if (!flashCanHoldPair(p))           p.status = FilePair::fp_tooBig;
        else if (flashMatchesPair(p))       p.status = FilePair::fp_loaded;
        else                                p.status = FilePair::fp_found;
      }
    }
  }

  /***
   *** State of the Sample Finder
   ***/

  const char* sampleFileSuffix = "";

  const auto c_notFound = CircuitPlayground.strip.Color(  0,   0,   0);
  const auto c_found    = CircuitPlayground.strip.Color(200, 200, 200);
  const auto c_loaded   = CircuitPlayground.strip.Color(  0, 250,   0);
  const auto c_tooBig   = CircuitPlayground.strip.Color(250,   0,   0);
  const auto c_selected = CircuitPlayground.strip.Color(100,   0,  250);

  int selectedPair = -1;

  void advanceSelection() {
    for (int i = selectedPair + 1; i < pairs.size(); ++i) {
      if (pairs[i].status == FilePair::fp_found) {
        selectedPair = i;
        return;
      }
    }
    selectedPair = -1;
  }
}

namespace SampleFinder {

  void setup(const char* suffix) {
    sampleFileSuffix = suffix;

    loadFlashedSamples();
  }

  void enter() {
    findFilePairs(sampleFileSuffix);
    selectedPair = -1;
  }

  void exit() {
  }

  void loop(millis_t now) {
    static bool rightPressed = false;
    if (rightPressed != CircuitPlayground.rightButton()) {
      rightPressed = CircuitPlayground.rightButton();
      if (rightPressed) {
        advanceSelection();
      }
    }

    static bool leftPressed = false;
    if (leftPressed != CircuitPlayground.leftButton()) {
      leftPressed = CircuitPlayground.leftButton();
      if (leftPressed) {
        if (selectedPair >= 0) {
          loadPairToFlash(pairs[selectedPair]);
          enter();
        }
      }
    }
  }

  void display(millis_t now) {
    bool anyFound = false;

    for (int i = 0; i < pairs.size(); ++i) {
      anyFound = anyFound || pairs[i].status != c_notFound;

      auto c = c_notFound;
      switch (pairs[i].status) {
        case FilePair::fp_notFound:   c = c_notFound; break;
        case FilePair::fp_found:      c = c_found;    break;
        case FilePair::fp_loaded:     c = c_loaded;   break;
        case FilePair::fp_tooBig:     c = c_tooBig;   break;
      }

      if (i == selectedPair) {
        if (now % 500 > 250) {
          c = c_selected;
        }
      }
      CircuitPlayground.strip.setPixelColor(i, c);
    }

    if (!anyFound) {
      CircuitPlayground.strip.setPixelColor(9, c_tooBig);
    }
  }


  bool newFlashSamplesAvailable() {
    return flashDirChanged;
  }

  FlashSamples flashSamples() {
    FlashSamples fs = {
      Samples(flashedDir.left.data, flashedDir.left.size),
      Samples(flashedDir.right.data, flashedDir.right.size)
      };

    statusMsgf("flashSamples left  %08x for %5d", flashedDir.left.data, flashedDir.left.size);
    statusMsgf("flashSamples right %08x for %5d", flashedDir.right.data, flashedDir.right.size);

    flashDirChanged = false;
    return fs;
  }

}

