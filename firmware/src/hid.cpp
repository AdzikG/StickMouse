#include "hid.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <freertos/queue.h>
#include <stdarg.h>

#include "remote.h"

namespace hid {
namespace {

constexpr uint8_t RID_MOUSE = 1, RID_KEYBOARD = 2, RID_CONSUMER = 3;
constexpr uint32_t FLUSH_INTERVAL_MS = 8;
constexpr uint32_t CHECK_INTERVAL_MS = 1000;
// HID host + remote control client + one spare, so a stale link cannot
// block advertising. NimBLE's default CONFIG_BT_NIMBLE_MAX_CONNECTIONS is 3.
constexpr int MAX_PEERS = 3;

// Log lines from the NimBLE task, printed from loop() to every client.
struct LogLine {
    char text[96];
};
QueueHandle_t logQueue = nullptr;

void logf(const char* fmt, ...) {
    LogLine l;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(l.text, sizeof(l.text), fmt, ap);
    va_end(ap);
    if (logQueue) xQueueSend(logQueue, &l, 0);
}

const uint8_t REPORT_MAP[] = {
    // Mouse: 3 buttons, X, Y, wheel, AC Pan
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, RID_MOUSE,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,  // buttons
    0x95, 0x01, 0x75, 0x05, 0x81, 0x03,  // padding
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,  // X, Y, wheel
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x06,  // AC Pan
    0xC0, 0xC0,

    // Keyboard: modifiers, reserved, 6 keys
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, RID_KEYBOARD,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    // Byte 1 (normally reserved): Apple Globe/Fn key, vendor page 0xFF usage 0x03.
    0x05, 0xFF, 0x09, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x95, 0x01, 0x81, 0x03,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x73,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x73, 0x81, 0x00,
    0xC0,

    // Consumer control: one 16-bit usage
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, RID_CONSUMER,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
};

NimBLEServer* server = nullptr;
NimBLEHIDDevice* device = nullptr;
NimBLECharacteristic* mouseIn = nullptr;
NimBLECharacteristic* keyboardIn = nullptr;
NimBLECharacteristic* consumerIn = nullptr;

uint8_t buttons = 0;
int pendingX = 0, pendingY = 0;
uint32_t lastFlush = 0;
uint32_t lastCheck = 0;

uint8_t publicAddr[6];
uint8_t identity = 0;
const char* advName = "";
// False until the controller accepted the identity's address; nothing is
// advertised meanwhile, so a host never sees the wrong identity.
// Read from the NimBLE task.
volatile bool addrOk = false;
volatile bool advFailed = false;
// HOST OFF: the device drops the host and stops advertising. Disconnecting
// from our side keeps the host's auto-reconnect, unlike a disconnect on the
// phone, so it comes back by itself on HOST ON.
volatile bool hostOn = true;

// The HID host is the encrypted (bonded) peer; the remote client over
// Web Bluetooth never pairs. Written from the NimBLE task.
volatile uint16_t hostHandle = BLE_HS_CONN_HANDLE_NONE;

bool canAdvertise(NimBLEServer* s) { return addrOk && hostOn && s->getConnectedCount() < MAX_PEERS; }

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        logf("LOG connect %s", info.getAddress().toString().c_str());
        // iPadOS-friendly: 15 ms interval, no latency, 4 s timeout.
        s->updateConnParams(info.getConnHandle(), 12, 12, 0, 400);
        // Advertising stops on connect; keep it up for the other peers.
        if (canAdvertise(s)) NimBLEDevice::startAdvertising();
    }
    void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
        logf("LOG disconnect %s reason=0x%X", info.getAddress().toString().c_str(), reason);
        remote::forget(info.getConnHandle());
        if (canAdvertise(s)) NimBLEDevice::startAdvertising();
        if (info.getConnHandle() != hostHandle) return;
        hostHandle = BLE_HS_CONN_HANDLE_NONE;
        buttons = 0;
        pendingX = pendingY = 0;
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        logf("LOG auth %s id=%s enc=%d bonded=%d", info.getAddress().toString().c_str(),
             info.getIdAddress().toString().c_str(), info.isEncrypted(), info.isBonded());
        if (!info.isEncrypted()) return;
        hostHandle = info.getConnHandle();
        // Connected just before HOST OFF stopped advertising.
        if (!hostOn) NimBLEDevice::getServer()->disconnect(info.getConnHandle());
    }
};

void disconnectHost() {
    uint16_t h = hostHandle;
    if (h == BLE_HS_CONN_HANDLE_NONE) return;
    server->disconnect(h);
    uint32_t t = millis();
    while (hostHandle != BLE_HS_CONN_HANDLE_NONE && millis() - t < 1000) delay(10);
}

void sendMouse(int8_t x, int8_t y, int8_t wheel, int8_t pan) {
    uint8_t r[5] = {buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel, (uint8_t)pan};
    mouseIn->setValue(r, sizeof(r));
    mouseIn->notify();
}

void sendKeyboard(uint16_t mods, uint8_t usage) {
    uint8_t r[8] = {(uint8_t)(mods & 0xFF), (uint8_t)((mods & MOD_GLOBE) ? 1 : 0), usage, 0, 0, 0, 0, 0};
    keyboardIn->setValue(r, sizeof(r));
    keyboardIn->notify();
}

constexpr uint16_t USAGE_GLOBE = 0x029D;  // AC Next Keyboard Layout Select

void sendConsumer(uint16_t usage) {
    uint8_t r[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
    consumerIn->setValue(r, 2);
    consumerIn->notify();
}

int8_t clamp8(int v) { return (int8_t)(v > 127 ? 127 : (v < -127 ? -127 : v)); }

// US layout: ASCII 0x20..0x7E -> usage, bit 7 = shift
const uint8_t ASCII_MAP[95] = {
    0x2C, 0x9E, 0xB4, 0xA0, 0xA1, 0xA2, 0xA4, 0x34, 0xA6, 0xA7, 0xA5, 0xAE, 0x36, 0x2D, 0x37, 0x38,  //  !"#$%&'()*+,-./
    0x27, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,                                      // 0-9
    0xB3, 0x33, 0xB6, 0x2E, 0xB7, 0xB8, 0x9F,                                                        // :;<=>?@
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90,                    // A-M
    0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D,                    // N-Z
    0x2F, 0x31, 0x30, 0xA3, 0xAD, 0x35,                                                              // [\]^_`
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,                    // a-m
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,                    // n-z
    0xAF, 0xB1, 0xB0, 0xB5,                                                                          // {|}~
};

// Identity 0 = public MAC; others = public MAC with identity mixed in,
// top two bits set = random static.
void identityAddress(uint8_t id, uint8_t* addr) {
    memcpy(addr, publicAddr, 6);
    if (id == 0) return;
    addr[0] ^= id;
    addr[5] |= 0xC0;
}

// The controller may refuse a new address (e.g. while advertising), keeping
// the old one; the address in use is read back to be sure.
bool applyAddress(uint8_t id) {
    uint8_t want[6];
    identityAddress(id, want);
    if (id == 0) {
        NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
    } else if (NimBLEDevice::setOwnAddr(want)) {
        NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    }
    NimBLEAddress now = NimBLEDevice::getAddress();
    bool ok = memcmp(now.getVal(), want, 6) == 0 && now.getType() == (id == 0 ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM);
    logf("LOG addr %s %s", now.toString().c_str(), ok ? "ok" : "FAILED");
    return ok;
}

void startAdvertising() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->clearData();
    adv->setAppearance(0x03C2);  // HID mouse
    adv->addServiceUUID(device->getHidService()->getUUID());
    adv->setName(advName);
    adv->enableScanResponse(true);
    bool ok = adv->start();
    if (!ok && !advFailed) logf("LOG adv start FAILED");
    advFailed = !ok;
}

// Retries a refused address and restarts advertising that stopped while a
// peer slot is free (it is not restarted after every failed connection).
void checkAdvertising(bool usbHost) {
    // Without a USB PC or the Bluetooth remote nobody could send HOST ON:
    // the device buttons are out of reach and nothing advertises for the remote.
    if (!hostOn && !usbHost && server->getConnectedCount() == 0) {
        hostOn = true;
        logf("LOG host on (no PC)");
    }
    if (!addrOk) {
        NimBLEDevice::stopAdvertising();
        addrOk = applyAddress(identity);
        if (!addrOk) return;
    }
    if (!canAdvertise(server) || NimBLEDevice::getAdvertising()->isAdvertising()) return;
    if (!advFailed) logf("LOG adv restart");
    startAdvertising();
}

}  // namespace

void setIdentity(uint8_t id, const char* name) {
    addrOk = false;  // no restart from the disconnect below
    NimBLEDevice::stopAdvertising();
    // The remote client stays connected under the old address.
    disconnectHost();
    buttons = 0;
    pendingX = pendingY = 0;
    identity = id;
    advName = name;
    hostOn = true;  // a new profile is picked to be used
    logf("LOG profile identity=%u name=\"%s\"", id, name);
    NimBLEDevice::setDeviceName(name);
    addrOk = applyAddress(id);
    if (addrOk) startAdvertising();
}

void setHostEnabled(bool on) {
    if (on == hostOn) return;
    hostOn = on;
    logf("LOG host %s", on ? "on" : "off");
    if (on) {
        checkAdvertising(true);
        return;
    }
    NimBLEDevice::stopAdvertising();
    disconnectHost();
    buttons = 0;
    pendingX = pendingY = 0;
}

bool hostEnabled() { return hostOn; }

const char* problem() {
    if (!addrOk) return "BLE: blad adresu";
    if (advFailed) return "BLE: blad reklamy";
    return nullptr;
}

void setBatteryLevel(uint8_t percent) { device->setBatteryLevel(percent, true); }

void begin(uint8_t id, const char* name) {
    logQueue = xQueueCreate(16, sizeof(LogLine));
    NimBLEDevice::init(name);
    // Read before any identity switches the address type to random.
    memcpy(publicAddr, NimBLEDevice::getAddress().getVal(), 6);
    NimBLEDevice::setPower(9);
    NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, no MITM, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    // Identity key distribution stays on: the identity address sent is the
    // profile's own connection address, and iOS needs it to auto-reconnect.

    server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    // Restarted by ServerCallbacks, only under the right address.
    server->advertiseOnDisconnect(false);

    device = new NimBLEHIDDevice(server);
    device->setManufacturer("M5Stack");
    device->setPnp(0x02, 0x303A, 0x8001, 0x0100);
    device->setHidInfo(0x00, 0x02);
    device->setReportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
    mouseIn = device->getInputReport(RID_MOUSE);
    keyboardIn = device->getInputReport(RID_KEYBOARD);
    consumerIn = device->getInputReport(RID_CONSUMER);
    device->setBatteryLevel(100);
    remote::attach(server);
    server->start();

    identity = id;
    advName = name;
    addrOk = applyAddress(id);
    if (addrOk) startAdvertising();
}

bool connected() { return hostHandle != BLE_HS_CONN_HANDLE_NONE; }

void move(int dx, int dy) {
    if (!connected()) return;
    pendingX += dx;
    pendingY += dy;
}

void press(uint8_t b) {
    if (!connected()) return;
    buttons |= b;
    sendMouse(0, 0, 0, 0);
}

void release(uint8_t b) {
    if (!connected()) return;
    buttons &= ~b;
    sendMouse(0, 0, 0, 0);
}

void click(uint8_t b) {
    press(b);
    delay(15);
    release(b);
}

void scroll(int v, int h) {
    if (!connected()) return;
    sendMouse(0, 0, clamp8(v), clamp8(h));
}

void key(uint16_t mods, uint8_t usage) {
    if (!connected()) return;
    bool globe = mods & MOD_GLOBE;
    if (globe) {
        // Globe is held before the key arrives, sent both as Apple vendor
        // bit and as consumer AC Next Keyboard Layout Select (third-party style).
        sendKeyboard(MOD_GLOBE, 0);
        sendConsumer(USAGE_GLOBE);
        delay(40);
    }
    sendKeyboard(mods, usage);
    delay(globe ? 60 : 20);
    sendKeyboard(globe ? MOD_GLOBE : 0, 0);
    if (globe) {
        delay(20);
        sendKeyboard(0, 0);
        sendConsumer(0);
    }
    delay(10);
}

void consumer(uint16_t usage, uint32_t holdMs) {
    if (!connected()) return;
    sendConsumer(usage);
    delay(holdMs);
    sendConsumer(0);
}

// Polish letters -> base letter usage (Polish Pro: ź = Alt+X, ż = Alt+Z).
struct PolishKey {
    uint16_t lower, upper;
    uint8_t usage;
};
const PolishKey POLISH_MAP[] = {
    {0x0105, 0x0104, 0x04},  // ą Ą -> A
    {0x0107, 0x0106, 0x06},  // ć Ć -> C
    {0x0119, 0x0118, 0x08},  // ę Ę -> E
    {0x0142, 0x0141, 0x0F},  // ł Ł -> L
    {0x0144, 0x0143, 0x11},  // ń Ń -> N
    {0x00F3, 0x00D3, 0x12},  // ó Ó -> O
    {0x015B, 0x015A, 0x16},  // ś Ś -> S
    {0x017A, 0x0179, 0x1B},  // ź Ź -> X
    {0x017C, 0x017B, 0x1D},  // ż Ż -> Z
};

// Decodes one UTF-8 sequence; advances p. Returns 0 for invalid input.
uint32_t nextCodepoint(const char*& p) {
    uint8_t c = (uint8_t)*p++;
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (extra < 0) return 0;
    uint32_t cp = c & (0x3F >> extra);
    for (int i = 0; i < extra; ++i) {
        if (((uint8_t)*p & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | ((uint8_t)*p++ & 0x3F);
    }
    return cp;
}

int typeText(const char* text, uint16_t polishMod) {
    int skipped = 0;
    for (const char* p = text; *p;) {
        uint32_t cp = nextCodepoint(p);
        if (cp >= 0x20 && cp <= 0x7E) {
            uint8_t m = ASCII_MAP[cp - 0x20];
            key((m & 0x80) ? MOD_SHIFT : 0, m & 0x7F);
            continue;
        }
        bool typed = false;
        for (const PolishKey& k : POLISH_MAP) {
            if (cp != k.lower && cp != k.upper) continue;
            key(polishMod | (cp == k.upper ? MOD_SHIFT : 0), k.usage);
            typed = true;
            break;
        }
        if (!typed) ++skipped;
    }
    return skipped;
}

void printBonds(Print& out) {
    int n = NimBLEDevice::getNumBonds();
    out.printf("BONDS %d\n", n);
    for (int i = 0; i < n; ++i) {
        out.printf("BOND %s\n", NimBLEDevice::getBondedAddress(i).toString().c_str());
    }
}

void clearBonds() {
    disconnectHost();
    NimBLEDevice::deleteAllBonds();
}

void printState(Print& out) {
    NimBLEAddress a = NimBLEDevice::getAddress();
    out.printf("BLE ADDR %s %s OK %d ADV %d PEERS %u HOST %d HOSTON %d\n", a.toString().c_str(),
               a.getType() == BLE_ADDR_PUBLIC ? "PUB" : "RND", addrOk ? 1 : 0,
               NimBLEDevice::getAdvertising()->isAdvertising() ? 1 : 0, server->getConnectedCount(),
               connected() ? 1 : 0, hostOn ? 1 : 0);
    for (uint8_t i = 0; i < server->getConnectedCount(); ++i) {
        NimBLEConnInfo p = server->getPeerInfo(i);
        out.printf("BLE PEER %s ENC %d BOND %d\n", p.getAddress().toString().c_str(), p.isEncrypted(),
                   p.isBonded());
    }
}

void update(Print& log, bool usbHost) {
    for (LogLine l; xQueueReceive(logQueue, &l, 0) == pdTRUE;) log.println(l.text);

    uint32_t now = millis();
    if (now - lastCheck >= CHECK_INTERVAL_MS) {
        lastCheck = now;
        checkAdvertising(usbHost);
    }
    if (now - lastFlush < FLUSH_INTERVAL_MS) return;
    lastFlush = now;
    if (!connected() || (pendingX == 0 && pendingY == 0)) return;
    int8_t x = clamp8(pendingX), y = clamp8(pendingY);
    pendingX -= x;
    pendingY -= y;
    sendMouse(x, y, 0, 0);
}

}  // namespace hid
