#pragma once
#include <Print.h>
#include <stdint.h>

namespace power {

// Call first in setup(), before M5.begin(): after a timer wake with USB power
// still missing it goes straight back to deep sleep without touching the display.
void earlyBoot();
void begin();
// Polls USB power; enters deep sleep once it has been missing for the grace period.
void update();

bool usbPresent();
// Remaining ms before sleep while USB power is missing; 0 = no countdown.
uint32_t shutdownInMs();

bool autoOff();
void setAutoOff(bool on);
void printStatus(Print& out);

}  // namespace power
