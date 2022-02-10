#include <Adafruit_CircuitPlayground.h>
#include "filesystem.h"

void setup() {
  CircuitPlayground.begin();

  while (!Serial) yield();

  Serial.println("Pandora's Drumming Box : File System Initializer");

  bool force = !CircuitPlayground.slideSwitch();
  Serial.printf("will force: %s\n", force ? "yes" : "no");

  bool ok = initFileSystem(force);

  if (ok) {
    Serial.println("Done!");
    Serial.println("Now flash with the PBB program.");
  }
  else {
    Serial.println(":-(");
  }
}

void loop() {
  delay(100);
}