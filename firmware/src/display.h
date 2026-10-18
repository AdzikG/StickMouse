#pragma once
#include <stdint.h>

namespace display {

void begin();
// Redraws only when something changed; also dims/blanks the backlight when idle.
void update(const char* hostLabel, bool host, bool pc, const char* lastAction);
// Wakes the screen and restarts the idle timer. Cheap: only stores a timestamp.
void activity();
// Seconds of inactivity before the backlight goes off (dims at half); 0 = always on.
uint32_t timeoutSec();
void setTimeoutSec(uint32_t sec);

}  // namespace display
