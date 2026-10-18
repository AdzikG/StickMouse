#include "protocol.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "hid.h"
#include "power.h"
#include "profile.h"
#include "remote.h"

namespace protocol {

void switchProfile(size_t index) {
    profile::select(index);
    const profile::Profile& p = profile::current();
    hid::setIdentity((uint8_t)index, p.bleName);
}

namespace {

uint8_t parseButton(const char* s) {
    if (!s) return hid::BTN_L;
    switch (s[0]) {
        case 'R': return hid::BTN_R;
        case 'M': return hid::BTN_M;
        default: return hid::BTN_L;
    }
}

}  // namespace

void handle(char* line, Print& out, char* action, size_t actionLen) {
    action[0] = '\0';
    while (*line == ' ') ++line;
    if (!*line) return;

    // Text command keeps the rest of the line verbatim.
    if (line[0] == 'T' && line[1] == ' ') {
        if (!hid::connected()) { out.println("ERR BLE"); return; }
        int skipped = hid::typeText(line + 2, profile::current().polishMod);
        snprintf(action, actionLen, "Tekst");
        if (skipped) out.printf("OK T skipped=%d\n", skipped);
        else out.println("OK T");
        return;
    }

    // CFG <profile> [text]: page settings; text is the rest of the line.
    if (strncmp(line, "CFG ", 4) == 0) {
        char* id = line + 4;
        char* text = strchr(id, ' ');
        if (text) *text++ = '\0';
        int i = profile::find(id);
        if (i < 0) { out.printf("ERR unknown profile %s\n", id); return; }
        if (text) {
            profile::setConfig((size_t)i, text);
            out.println("OK CFG");
        } else {
            char buf[200];
            profile::config((size_t)i, buf, sizeof(buf));
            out.printf("CFG %s %s\n", id, buf);
        }
        return;
    }

    char* save = nullptr;
    char* cmd = strtok_r(line, " ", &save);
    char* a1 = strtok_r(nullptr, " ", &save);
    char* a2 = strtok_r(nullptr, " ", &save);

    if (strcmp(cmd, "PING") == 0) {
        out.println("PONG");
        out.printf("STATUS BLE %d\n", hid::connected() ? 1 : 0);
        out.printf("PROFILE %s\n", profile::current().id);
        return;
    }

    if (strcmp(cmd, "BONDS") == 0) {
        if (a1 && strcmp(a1, "CLEAR") == 0) {
            hid::clearBonds();
            snprintf(action, actionLen, "Parowanie wyczysz.");
        }
        hid::printBonds(out);
        return;
    }

    // Only logged-in Bluetooth clients and USB get here (see remote.cpp).
    if (strcmp(cmd, "AUTH") == 0) {
        out.println("OK AUTH");
        return;
    }

    if (strcmp(cmd, "PIN") == 0) {
        if (a1 && strcmp(a1, "NEW") == 0) remote::newPin();
        if (a1 && strcmp(a1, "SET") == 0 && !(a2 && remote::setPin(a2))) {
            out.println("ERR PIN must be 6 digits");
            return;
        }
        out.printf("PIN %s\n", remote::pin());
        return;
    }

    if (strcmp(cmd, "BLE") == 0) {
        hid::printState(out);
        return;
    }

    if (strcmp(cmd, "HOST") == 0) {
        if (a1) {
            hid::setHostEnabled(strcmp(a1, "ON") == 0);
            snprintf(action, actionLen, "%s %s", profile::current().label, hid::hostEnabled() ? "wlaczony" : "odlaczony");
        }
        out.printf("HOST %s\n", hid::hostEnabled() ? "ON" : "OFF");
        return;
    }

    if (strcmp(cmd, "SCREEN") == 0) {
        if (a1) display::setTimeoutSec((uint32_t)atoi(a1));
        out.printf("SCREEN %u\n", (unsigned)display::timeoutSec());
        return;
    }

    if (strcmp(cmd, "AUTOOFF") == 0) {
        if (a1) power::setAutoOff(atoi(a1) != 0);
        out.printf("AUTOOFF %d\n", power::autoOff() ? 1 : 0);
        return;
    }

    if (strcmp(cmd, "POWER") == 0) {
        power::printStatus(out);
        return;
    }

    if (strcmp(cmd, "P") == 0) {
        if (a1) {
            int i = profile::find(a1);
            if (i < 0) { out.printf("ERR unknown profile %s\n", a1); return; }
            if ((size_t)i != profile::currentIndex()) switchProfile((size_t)i);
            snprintf(action, actionLen, "Profil %s", profile::current().label);
        }
        out.printf("PROFILE %s\n", profile::current().id);
        hid::printState(out);
        return;
    }

    if (!hid::connected()) {
        if (strcmp(cmd, "M") != 0) out.println("ERR BLE");
        return;
    }

    if (strcmp(cmd, "M") == 0) {
        hid::move(a1 ? atoi(a1) : 0, a2 ? atoi(a2) : 0);
    } else if (strcmp(cmd, "C") == 0) {
        hid::click(parseButton(a1));
        snprintf(action, actionLen, "Klik %s", a1 ? a1 : "L");
        out.println("OK C");
    } else if (strcmp(cmd, "D") == 0) {
        hid::press(parseButton(a1));
        snprintf(action, actionLen, "Wcisniety %s", a1 ? a1 : "L");
        out.println("OK D");
    } else if (strcmp(cmd, "U") == 0) {
        hid::release(parseButton(a1));
        snprintf(action, actionLen, "Puszczony %s", a1 ? a1 : "L");
        out.println("OK U");
    } else if (strcmp(cmd, "S") == 0) {
        hid::scroll(a1 ? atoi(a1) : 0, a2 ? atoi(a2) : 0);
        snprintf(action, actionLen, "Scroll");
    } else if (strcmp(cmd, "K") == 0 && a1) {
        const profile::Profile& p = profile::current();
        for (size_t i = 0; i < p.shortcutCount; ++i) {
            const profile::Shortcut& s = p.shortcuts[i];
            if (strcmp(s.name, a1) != 0) continue;
            if (s.usage) hid::key(s.mods, s.usage);
            else hid::consumer(s.consumer, s.holdMs ? s.holdMs : 30);
            snprintf(action, actionLen, "%s", s.name);
            out.printf("OK K %s\n", s.name);
            return;
        }
        out.printf("ERR unknown key %s\n", a1);
    } else if (strcmp(cmd, "KK") == 0 && a1 && a2) {
        // Diagnostics: KK <hex modifiers> <hex usage>, e.g. KK 08 16 = Meta+S.
        uint16_t mods = (uint16_t)strtoul(a1, nullptr, 16);
        uint8_t usage = (uint8_t)strtoul(a2, nullptr, 16);
        hid::key(mods, usage);
        out.printf("OK KK %02X %02X\n", mods, usage);
    } else if (strcmp(cmd, "KC") == 0 && a1) {
        // Diagnostics: KC <hex usage> [hold ms], to find what a host reacts to.
        uint16_t usage = (uint16_t)strtoul(a1, nullptr, 16);
        uint32_t hold = a2 ? (uint32_t)atoi(a2) : 30;
        if (hold > 3000) hold = 3000;
        hid::consumer(usage, hold);
        out.printf("OK KC %04X %u\n", usage, (unsigned)hold);
    } else {
        out.printf("ERR unknown %s\n", cmd);
    }
}

}  // namespace protocol
