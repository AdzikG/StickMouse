#pragma once
#include <Print.h>

namespace protocol {

// Executes one command line (without '\n'). Responses go to `out`.
// `action` receives a short description for the display (empty for motion).
void handle(char* line, Print& out, char* action, size_t actionLen);

// Persists the profile and switches BLE identity to it.
void switchProfile(size_t index);

}  // namespace protocol
