#pragma once
#include <Print.h>
#include <stdint.h>

namespace hid {

enum Button : uint8_t { BTN_L = 1, BTN_R = 2, BTN_M = 4 };

// identity 0 = public address, others = derived random static address,
// so each host bonds to a separate "device".
void begin(uint8_t identity, const char* name);
// Drops the current host and re-advertises under another identity.
// Also turns the host back on.
void setIdentity(uint8_t identity, const char* name);
bool connected();

// Off: drops the host and stops advertising, without the host forgetting to
// auto-reconnect. Turns itself back on with neither a USB computer nor the
// Bluetooth remote connected, so it can never get stuck off.
void setHostEnabled(bool on);
bool hostEnabled();
// Short text for the screen when advertising is broken, else nullptr.
const char* problem();
// Battery Service level reported to the host.
void setBatteryLevel(uint8_t percent);

// Movement is accumulated and flushed from update() at a fixed rate.
void move(int dx, int dy);
void press(uint8_t buttons);
void release(uint8_t buttons);
void click(uint8_t buttons);
void scroll(int v, int h);

// Keyboard: modifier bitmask (HID, plus MOD_GLOBE) + usage code.
void key(uint16_t modifiers, uint8_t usage);
// Consumer control usage (e.g. 0x0223 AC Home), held for holdMs.
void consumer(uint16_t usage, uint32_t holdMs = 30);
// UTF-8 text. Polish letters are typed as `polishMod`+letter (Polish Pro layout:
// Option on iPadOS, Right Alt on Android). Returns number of characters skipped.
int typeText(const char* text, uint16_t polishMod);

void clearBonds();
void printBonds(Print& out);
// Address in use, advertising and connected peers (BLE command).
void printState(Print& out);
// Flushes movement, keeps advertising alive, prints BLE log lines to `log`.
// usbHost: USB is connected to a computer (not just a charger).
void update(Print& log, bool usbHost);

// HID modifier bits
constexpr uint16_t MOD_CTRL = 0x01, MOD_SHIFT = 0x02, MOD_ALT = 0x04, MOD_GUI = 0x08;
constexpr uint16_t MOD_RALT = 0x40;  // AltGr
constexpr uint16_t MOD_GLOBE = 0x100;  // Apple Globe/Fn, not a standard HID modifier

}  // namespace hid
