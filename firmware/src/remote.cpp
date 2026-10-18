#include "remote.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_random.h>

namespace remote {
namespace {

// Nordic UART Service: RX = client writes commands, TX = notifications back.
// UUIDs must match web/touchpad.html.
const char* SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char* RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
const char* TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

// The channel is not encrypted (bonding would make phones and PCs use the HID
// service as hosts), so a client must first send "AUTH <pin>". Until then its
// writes are dropped and it gets no notifications.
constexpr size_t MAX_CLIENTS = 3;
uint16_t authed[MAX_CLIENTS] = {BLE_HS_CONN_HANDLE_NONE, BLE_HS_CONN_HANDLE_NONE, BLE_HS_CONN_HANDLE_NONE};
char pinCode[7] = "";
Preferences prefs;

// Filled from the NimBLE task, drained from loop().
uint8_t rxBuf[2048];
size_t rxHead = 0, rxTail = 0;
// AUTH results waiting for loop() to answer: handle, ok.
struct Reply {
    uint16_t handle;
    bool ok;
};
Reply replies[4];
size_t replyCount = 0;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

NimBLECharacteristic* tx = nullptr;

// Callers hold `lock`.
bool isAuthed(uint16_t h) {
    for (uint16_t a : authed)
        if (a == h) return true;
    return false;
}

void setAuthed(uint16_t h) {
    for (uint16_t& a : authed) {
        if (a == BLE_HS_CONN_HANDLE_NONE) {
            a = h;
            return;
        }
    }
}

void push(const uint8_t* data, size_t len) {
    size_t used = (rxHead + sizeof(rxBuf) - rxTail) % sizeof(rxBuf);
    // Drop the whole write rather than a fragment of a command.
    if (len >= sizeof(rxBuf) - 1 - used) return;
    for (size_t i = 0; i < len; ++i) {
        rxBuf[rxHead] = data[i];
        rxHead = (rxHead + 1) % sizeof(rxBuf);
    }
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        NimBLEAttValue v = c->getValue();
        const uint8_t* d = v.data();
        size_t len = v.length();
        uint16_t h = info.getConnHandle();
        portENTER_CRITICAL(&lock);
        if (!isAuthed(h)) {
            // Look for "AUTH <pin>\n"; anything before it is dropped, the rest
            // of the same write (the page queues several lines) goes through.
            size_t start = 0;
            for (size_t i = 0; i < len; ++i) {
                if (d[i] != '\n') continue;
                size_t n = i - start;
                if (n > 0 && d[i - 1] == '\r') --n;
                if (n >= 5 && memcmp(d + start, "AUTH ", 5) == 0) {
                    bool ok = n - 5 == strlen(pinCode) && memcmp(d + start + 5, pinCode, n - 5) == 0;
                    if (ok) setAuthed(h);
                    if (replyCount < sizeof(replies) / sizeof(replies[0])) replies[replyCount++] = {h, ok};
                    if (ok) {
                        d += i + 1;
                        len -= i + 1;
                        break;
                    }
                }
                start = i + 1;
            }
            if (!isAuthed(h)) len = 0;
        }
        push(d, len);
        portEXIT_CRITICAL(&lock);
    }
};

class TxCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info, uint16_t sub) override {
        Serial.printf("LOG remote %s %s mtu=%u\n", sub ? "on" : "off", info.getAddress().toString().c_str(),
                      info.getMTU());
    }
};

// Sends to one client in MTU-sized pieces; the page splits on '\n'.
void notifyTo(uint16_t h, const uint8_t* data, size_t len) {
    uint16_t mtu = NimBLEDevice::getServer()->getPeerInfoByHandle(h).getMTU();
    size_t payload = mtu > 23 ? mtu - 3 : 20;
    for (size_t off = 0; off < len; off += payload) {
        size_t n = len - off < payload ? len - off : payload;
        tx->notify(data + off, n, h);
    }
}

// Buffers one line, then sends it to every authenticated client.
class Out : public Print {
public:
    size_t write(uint8_t c) override {
        buf[len++] = c;
        if (c == '\n' || len == sizeof(buf)) send();
        return 1;
    }

private:
    void send() {
        uint16_t to[MAX_CLIENTS];
        portENTER_CRITICAL(&lock);
        memcpy(to, authed, sizeof(to));
        portEXIT_CRITICAL(&lock);
        for (uint16_t h : to)
            if (h != BLE_HS_CONN_HANDLE_NONE) notifyTo(h, buf, len);
        len = 0;
    }
    uint8_t buf[128];
    size_t len = 0;
};

Out output;

void generatePin() {
    snprintf(pinCode, sizeof(pinCode), "%06u", (unsigned)(esp_random() % 1000000));
    prefs.putString("pin", pinCode);
}

}  // namespace

void attach(NimBLEServer* server) {
    prefs.begin("bridge", false);
    if (prefs.isKey("pin")) prefs.getString("pin", pinCode, sizeof(pinCode));
    if (strlen(pinCode) != 6) generatePin();

    NimBLEService* svc = server->createService(SERVICE_UUID);
    NimBLECharacteristic* rx =
        svc->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(new RxCallbacks());
    tx = svc->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
    tx->setCallbacks(new TxCallbacks());
}

void update(Print& log) {
    Reply pending[sizeof(replies) / sizeof(replies[0])];
    size_t n;
    portENTER_CRITICAL(&lock);
    n = replyCount;
    memcpy(pending, replies, n * sizeof(Reply));
    replyCount = 0;
    portEXIT_CRITICAL(&lock);
    for (size_t i = 0; i < n; ++i) {
        // A failed client is not authenticated, so it is answered directly.
        static const char OK[] = "OK AUTH\n", ERR[] = "ERR AUTH\n";
        if (pending[i].ok) notifyTo(pending[i].handle, (const uint8_t*)OK, sizeof(OK) - 1);
        else notifyTo(pending[i].handle, (const uint8_t*)ERR, sizeof(ERR) - 1);
        log.printf("LOG remote auth %s\n", pending[i].ok ? "ok" : "FAILED");
    }
}

void forget(uint16_t connHandle) {
    portENTER_CRITICAL(&lock);
    for (uint16_t& a : authed)
        if (a == connHandle) a = BLE_HS_CONN_HANDLE_NONE;
    portEXIT_CRITICAL(&lock);
}

const char* pin() { return pinCode; }

// Clients already logged in stay; the new PIN applies to the next connections.
void newPin() { generatePin(); }

int read() {
    int c = -1;
    portENTER_CRITICAL(&lock);
    if (rxTail != rxHead) {
        c = rxBuf[rxTail];
        rxTail = (rxTail + 1) % sizeof(rxBuf);
    }
    portEXIT_CRITICAL(&lock);
    return c;
}

Print& out() { return output; }

}  // namespace remote
