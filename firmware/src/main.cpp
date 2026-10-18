#include <M5Unified.h>

#include "display.h"
#include "hid.h"
#include "power.h"
#include "profile.h"
#include "protocol.h"
#include "remote.h"

namespace {

constexpr uint32_t PC_TIMEOUT_MS = 3000;  // web page sends PING every second
constexpr uint32_t CLEAR_BONDS_HOLD_MS = 3000;

// One per command source; responses go back to the same source.
struct LineReader {
    char buf[256];
    size_t len = 0;
};
LineReader serialIn, remoteIn;

// Status changes go to every client: USB serial and BLE remote.
class Broadcast : public Print {
public:
    size_t write(uint8_t c) override {
        Serial.write(c);
        remote::out().write(c);
        return 1;
    }
} broadcast;

char lastAction[32] = "Czekam na parowanie";
uint32_t lastCommandAt = 0;
bool lastBle = false;
bool lastPc = false;
bool lastUsb = true;
uint32_t lastCountdown = 0;
const char* lastProblem = nullptr;
uint32_t lastBatteryAt = 0;
int lastBattery = -1;

constexpr uint32_t BATTERY_INTERVAL_MS = 60000;

void feed(LineReader& in, int c, Print& out) {
    if (c == '\r') return;
    if (c == '\n') {
        in.buf[in.len] = '\0';
        // The page pings every second; only real commands keep the screen on.
        if (strncmp(in.buf, "PING", 4) != 0) display::activity();
        char action[32];
        protocol::handle(in.buf, out, action, sizeof(action));
        if (action[0]) strncpy(lastAction, action, sizeof(lastAction) - 1);
        in.len = 0;
        lastCommandAt = millis();
    } else if (in.len < sizeof(in.buf) - 1) {
        in.buf[in.len++] = (char)c;
    }
}

void readInputs() {
    while (Serial.available()) feed(serialIn, Serial.read(), Serial);
    remote::update(broadcast);
    for (int c; (c = remote::read()) >= 0;) feed(remoteIn, c, remote::out());
}

void updatePower() {
    power::update();
    bool usb = power::usbPresent();
    if (usb != lastUsb) {
        lastUsb = usb;
        display::activity();
        broadcast.printf("STATUS USB %d\n", usb ? 1 : 0);
        if (usb) strncpy(lastAction, "Zasilanie USB OK", sizeof(lastAction) - 1);
    }
    uint32_t left = power::shutdownInMs();
    uint32_t sec = (left + 999) / 1000;
    if (sec != lastCountdown) {
        lastCountdown = sec;
        if (sec) snprintf(lastAction, sizeof(lastAction), "Brak USB: uspie %us", (unsigned)sec);
    }
    if (left) display::activity();  // keep the warning visible
}

// Battery Service for the host's battery widget; notified only on change.
void updateBattery() {
    uint32_t now = millis();
    if (lastBatteryAt && now - lastBatteryAt < BATTERY_INTERVAL_MS) return;
    lastBatteryAt = now;
    int level = M5.Power.getBatteryLevel();
    if (level < 0 || level > 100 || level == lastBattery) return;
    lastBattery = level;
    hid::setBatteryLevel((uint8_t)level);
}

}  // namespace

void setup() {
    power::earlyBoot();

    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);  // never block when no host is reading

    display::begin();
    power::begin();
    profile::begin();
    hid::begin((uint8_t)profile::currentIndex(), profile::current().bleName);
}

void loop() {
    M5.update();
    readInputs();
    // isPlugged: USB frames from a computer, so a charger does not count.
    hid::update(broadcast, HWCDC::isPlugged());
    updateBattery();

    const char* problem = hid::problem();
    if (problem != lastProblem) {
        lastProblem = problem;
        display::activity();
        strncpy(lastAction, problem ? problem : hid::connected() ? "Polaczono" : "Czekam na parowanie",
                sizeof(lastAction) - 1);
    }

    bool ble = hid::connected();
    if (ble != lastBle) {
        lastBle = ble;
        display::activity();
        broadcast.printf("STATUS BLE %d\n", ble ? 1 : 0);
        strncpy(lastAction, ble ? "Polaczono" : "Czekam na parowanie", sizeof(lastAction) - 1);
    }

    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) display::activity();

    if (M5.BtnB.wasClicked()) {
        protocol::switchProfile((profile::currentIndex() + 1) % profile::count());
        broadcast.printf("PROFILE %s\n", profile::current().id);
        hid::printState(broadcast);
        snprintf(lastAction, sizeof(lastAction), "Profil %s", profile::current().label);
    }

    if (M5.BtnA.pressedFor(CLEAR_BONDS_HOLD_MS)) {
        hid::clearBonds();
        strncpy(lastAction, "Parowanie wyczysz.", sizeof(lastAction) - 1);
        while (M5.BtnA.isPressed()) { M5.update(); delay(10); }
    }

    updatePower();

    bool pc = lastCommandAt && millis() - lastCommandAt < PC_TIMEOUT_MS;
    if (pc != lastPc) {
        lastPc = pc;
        display::activity();
    }
    display::update(profile::current().label, ble, pc, lastAction);
    delay(1);
}
