#pragma once
#include <stddef.h>
#include <stdint.h>

namespace profile {

struct Shortcut {
    const char* name;
    uint16_t mods;      // keyboard modifiers (hid::MOD_*)
    uint8_t usage;      // keyboard usage; 0 = use consumer
    uint16_t consumer;  // consumer usage
    uint16_t holdMs;    // consumer hold time; 0 = default short press
};

struct Profile {
    const char* id;       // protocol id, e.g. "IPAD"
    const char* label;    // shown on the display
    const char* bleName;  // advertised name
    const Shortcut* shortcuts;
    size_t shortcutCount;
    uint16_t polishMod;   // modifier for Polish letters (host layout: Polish Pro)
};

// Loads the saved profile index from NVS.
void begin();
const Profile& current();
size_t currentIndex();
size_t count();
const Profile& at(size_t i);
// Returns index or -1.
int find(const char* id);
// Persists the choice; caller applies it to BLE.
void select(size_t i);

// Web page settings for a profile, opaque text kept in NVS ("" if none),
// so they follow the device to any browser or PC.
void config(size_t i, char* buf, size_t len);
void setConfig(size_t i, const char* text);

}  // namespace profile
