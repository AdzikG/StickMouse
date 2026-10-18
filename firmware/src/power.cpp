#include "power.h"

#include <M5Unified.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_sleep.h>

namespace power {
namespace {

constexpr uint32_t USB_LOSS_GRACE_MS = 30000;
// Deep sleep instead of PM1 power-off: power-off may need the side button to
// come back, deep sleep wakes by itself and checks USB again.
constexpr uint64_t USB_CHECK_INTERVAL_US = 60ULL * 1000 * 1000;
constexpr uint32_t POLL_MS = 500;
constexpr uint16_t VBUS_MIN_MV = 4000;

// M5PM1 on the internal I2C bus (same as M5GFX autodetect for StickS3).
constexpr uint8_t PM1_ADDR = 0x6E, PM1_REG_VIN_L = 0x24;
constexpr int PM1_SDA = 47, PM1_SCL = 48;

Preferences prefs;
bool enabled = true;
bool present = true;
bool counting = false;
uint32_t lossAt = 0, lastPoll = 0;
int bootVbus = -1;  // raw early-boot reading, -1 = read failed (diagnostics)

// Reads VIN straight from PM1, without M5.begin() lighting up the display.
bool readVbusRaw(uint16_t& mv) {
    Wire.begin(PM1_SDA, PM1_SCL, 100000);
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        if (attempt) delay(5);
        Wire.beginTransmission(PM1_ADDR);
        Wire.write(PM1_REG_VIN_L);
        if (Wire.endTransmission(false) != 0) continue;
        if (Wire.requestFrom(PM1_ADDR, (uint8_t)2) != 2) continue;
        mv = Wire.read();
        mv |= Wire.read() << 8;
        ok = true;
    }
    Wire.end();
    return ok;
}

[[noreturn]] void sleepUntilCheck() {
    esp_sleep_enable_timer_wakeup(USB_CHECK_INTERVAL_US);
    esp_deep_sleep_start();
}

void enterSleep() {
    Serial.println("LOG sleep: no USB power");
    Serial.flush();
    M5.Display.setBrightness(0);
    M5.Display.sleep();
    M5.Display.waitDisplay();
    sleepUntilCheck();
}

}  // namespace

void earlyBoot() {
    // Read on every boot so POWER can compare it with the M5Unified value.
    uint16_t mv = 0;
    bool ok = readVbusRaw(mv);
    bootVbus = ok ? mv : -1;
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) return;
    // On a read failure boot normally; update() sleeps again after the grace period.
    if (ok && mv < VBUS_MIN_MV) sleepUntilCheck();
}

void begin() {
    prefs.begin("bridge", false);
    enabled = prefs.getBool("autooff", true);
}

void update() {
    uint32_t now = millis();
    if (now - lastPoll < POLL_MS) return;
    lastPoll = now;

    bool p = M5.Power.getVBUSVoltage() >= VBUS_MIN_MV;
    present = p;
    if (present || !enabled) {
        counting = false;
        return;
    }
    if (!counting) {
        counting = true;
        lossAt = now;
    }
    if (now - lossAt >= USB_LOSS_GRACE_MS) enterSleep();
}

bool usbPresent() { return present; }

uint32_t shutdownInMs() {
    if (!counting) return 0;
    uint32_t elapsed = millis() - lossAt;
    return elapsed >= USB_LOSS_GRACE_MS ? 1 : USB_LOSS_GRACE_MS - elapsed;
}

bool autoOff() { return enabled; }

void setAutoOff(bool on) {
    enabled = on;
    prefs.putBool("autooff", on);
    if (!on) counting = false;
}

void printStatus(Print& out) {
    out.printf("POWER VBUS %d BAT %d AUTOOFF %d BOOTVBUS %d WAKE %d UP %lu LVL %d\n",
               M5.Power.getVBUSVoltage(), M5.Power.getBatteryVoltage(), enabled ? 1 : 0, bootVbus,
               (int)esp_sleep_get_wakeup_cause(), (unsigned long)(millis() / 1000),
               (int)M5.Power.getBatteryLevel());
}

}  // namespace power
