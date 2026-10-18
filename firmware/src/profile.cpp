#include "profile.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>

#include "hid.h"

namespace profile {
namespace {

using hid::MOD_ALT;
using hid::MOD_GLOBE;
using hid::MOD_GUI;
using hid::MOD_RALT;
using hid::MOD_SHIFT;

// Shared navigation keys.
#define COMMON_KEYS                       \
    {"VOLUP", 0, 0, 0x00E9},              \
    {"VOLDOWN", 0, 0, 0x00EA},            \
    {"MUTE", 0, 0, 0x00E2},               \
    {"PLAY", 0, 0, 0x00CD},               \
    {"BRIUP", 0, 0, 0x006F},              /* works on iPadOS */ \
    {"BRIDOWN", 0, 0, 0x0070},            \
    {"ENTER", 0, 0x28, 0},                \
    {"ESC", 0, 0x29, 0},                  \
    {"BACKSPACE", 0, 0x2A, 0},            \
    {"TAB", 0, 0x2B, 0},                  \
    {"RIGHT", 0, 0x4F, 0},                \
    {"LEFT", 0, 0x50, 0},                 \
    {"DOWN", 0, 0x51, 0},                 \
    {"UP", 0, 0x52, 0}

// Mappings marked (?) still need verification on a real device.
const Shortcut IPAD_KEYS[] = {
    {"HOME", MOD_GLOBE, 0x0B, 0},                // Globe+H, works in App Switcher too (?)
    {"HOME2", MOD_GUI, 0x0B, 0},                 // Cmd+H, only from inside an app
    {"APPSWITCH", MOD_GLOBE, 0x52, 0},           // Globe+Up -> App Switcher grid
    {"LASTAPP", MOD_GUI, 0x2B, 0},               // Cmd+Tab -> previous app
    {"NOTIF", MOD_GLOBE, 0x11, 0},               // Globe+N -> Notification Center (?)
    {"CONTROL", MOD_GLOBE, 0x06, 0},             // Globe+C -> Control Center (?)
    {"SEARCH", MOD_GUI, 0x2C, 0},                // Cmd+Space
    {"ASSIST", MOD_GLOBE, 0x16, 0},              // Globe+S -> Siri (consumer 0xCF was ignored)
    {"SCREENSHOT", MOD_GUI | MOD_SHIFT, 0x20, 0},  // Cmd+Shift+3
    {"BACK", MOD_GUI, 0x2F, 0},                  // Cmd+[
    // AL Terminal Lock held 1 s; a 30 ms press, Power (0x30) and Sleep (0x32) are ignored.
    {"LOCK", 0, 0, 0x019E, 1000},
    COMMON_KEYS,
};

const Shortcut ANDROID_KEYS[] = {
    {"HOME", 0, 0, 0x0223},          // AC Home -> HOME
    {"APPSWITCH", 0, 0, 0x029F},     // AC Desktop Show All Windows -> APP_SWITCH (?)
    {"LASTAPP", MOD_ALT, 0x2B, 0},   // Alt+Tab -> previous app (?)
    {"SEARCH", 0, 0, 0x0221},        // AC Search -> SEARCH
    {"ASSIST", 0, 0, 0x00CF},        // Voice Command -> VOICE_ASSIST (?)
    {"SCREENSHOT", 0, 0x46, 0},      // PrintScreen -> SYSRQ
    {"BACK", 0, 0, 0x0224},          // AC Back -> BACK
    {"NOTIF", MOD_GUI, 0x11, 0},     // Meta+N -> notifications (?)
    {"LOCK", 0, 0, 0x0030},          // Power -> screen off (?)
    COMMON_KEYS,
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

const Profile PROFILES[] = {
    // BLE names stay as-is so existing pairings keep their familiar names.
    {"IPAD", "iPadOS", "iPad Bridge", IPAD_KEYS, COUNT(IPAD_KEYS), MOD_ALT},
    {"ANDROID", "Android", "Bridge Android", ANDROID_KEYS, COUNT(ANDROID_KEYS), MOD_RALT},
};

size_t active = 0;
Preferences prefs;

}  // namespace

void begin() {
    prefs.begin("bridge", false);
    active = prefs.getUChar("profile", 0);
    if (active >= COUNT(PROFILES)) active = 0;
}

const Profile& current() { return PROFILES[active]; }
size_t currentIndex() { return active; }
size_t count() { return COUNT(PROFILES); }
const Profile& at(size_t i) { return PROFILES[i]; }

int find(const char* id) {
    for (size_t i = 0; i < COUNT(PROFILES); ++i) {
        if (strcmp(PROFILES[i].id, id) == 0) return (int)i;
    }
    return -1;
}

void select(size_t i) {
    if (i >= COUNT(PROFILES)) return;
    active = i;
    prefs.putUChar("profile", (uint8_t)i);
}

namespace {
void configKey(size_t i, char* key) { snprintf(key, 8, "cfg%u", (unsigned)i); }
}  // namespace

void config(size_t i, char* buf, size_t len) {
    char key[8];
    configKey(i, key);
    buf[0] = '\0';
    if (prefs.isKey(key)) prefs.getString(key, buf, len);
}

void setConfig(size_t i, const char* text) {
    if (i >= COUNT(PROFILES)) return;
    char key[8], old[200];
    configKey(i, key);
    config(i, old, sizeof(old));
    if (strcmp(old, text) != 0) prefs.putString(key, text);  // sliders send often; spare the flash
}

}  // namespace profile
