#pragma once
#include <Print.h>
#include <stdint.h>

class NimBLEServer;

// Command channel over BLE (Nordic UART Service) for a PC or phone without
// USB: same text protocol as the serial port, alongside the HID service.
// A client must send "AUTH <pin>" first; USB needs no PIN (physical access).
namespace remote {

// Adds the service and loads (or creates) the PIN; call before the server is started.
void attach(NimBLEServer* server);
// Answers AUTH attempts; call from loop(). Log lines go to `log`.
void update(Print& log);
// Drops the client's login on disconnect.
void forget(uint16_t connHandle);
// 6-digit PIN, kept in NVS.
const char* pin();
void newPin();
// Next received byte from logged-in clients, -1 if none. Call from loop().
int read();
// Responses to logged-in clients, sent as notifications.
Print& out();

}  // namespace remote
