#include "display.h"

#include <M5Unified.h>
#include <Preferences.h>

namespace display {
namespace {

constexpr uint32_t SCREEN_TIMEOUT_SEC = 60;  // default, dims at half
constexpr uint8_t BRIGHT = 80, DIM = 12;

Preferences prefs;
uint32_t timeoutMs = SCREEN_TIMEOUT_SEC * 1000;
uint32_t lastActivity = 0;
uint8_t brightness = BRIGHT;

int lastHost = -1, lastPc = -1;
const char* lastLabel = nullptr;
char lastText[32] = "";

void row(int y, const char* label, bool ok) {
    M5.Display.fillRect(0, y, M5.Display.width(), 34, ok ? TFT_DARKGREEN : TFT_MAROON);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, y + 9);
    M5.Display.print(label);
    M5.Display.print(ok ? ": OK" : ": --");
}

// Backlight only (PWM, no bus traffic), so waking never delays mouse reports.
void applyBrightness() {
    uint8_t want = BRIGHT;
    if (timeoutMs) {
        uint32_t idle = millis() - lastActivity;
        if (idle >= timeoutMs) want = 0;
        else if (idle >= timeoutMs / 2) want = DIM;
    }
    if (want != brightness) {
        brightness = want;
        M5.Display.setBrightness(want);
    }
}

}  // namespace

void begin() {
    prefs.begin("bridge", false);
    timeoutMs = prefs.getUInt("screen_s", SCREEN_TIMEOUT_SEC) * 1000;
    M5.Display.setRotation(1);
    M5.Display.setBrightness(BRIGHT);
    M5.Display.fillScreen(TFT_BLACK);
    lastActivity = millis();
}

void activity() { lastActivity = millis(); }

uint32_t timeoutSec() { return timeoutMs / 1000; }

void setTimeoutSec(uint32_t sec) {
    timeoutMs = sec * 1000;
    prefs.putUInt("screen_s", sec);
    activity();
}

void update(const char* hostLabel, bool host, bool pc, const char* lastAction) {
    if (host != lastHost || hostLabel != lastLabel) {
        row(0, hostLabel, host);
        lastHost = host;
        lastLabel = hostLabel;
    }
    if (pc != lastPc) {
        row(38, "PC", pc);
        lastPc = pc;
    }
    if (strncmp(lastText, lastAction, sizeof(lastText) - 1) != 0) {
        strncpy(lastText, lastAction, sizeof(lastText) - 1);
        int y = 80;
        M5.Display.fillRect(0, y, M5.Display.width(), M5.Display.height() - y, TFT_BLACK);
        M5.Display.setTextColor(TFT_LIGHTGREY);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(8, y + 8);
        M5.Display.print(lastText);
    }
    applyBrightness();
}

}  // namespace display
