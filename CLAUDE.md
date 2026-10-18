# iPad Bridge — sterowanie iPadem z Windowsa przez M5StickS3

> Plik kontekstowy dla Claude Code. Umieść go w katalogu głównym repozytorium.
> Opisuje cel projektu, podjęte decyzje, architekturę i plan prac.

---

## 1. Cel projektu

Umożliwić sterowanie iPadem z komputera z Windowsem (w tym **służbowego, bez praw administratora**).
Cała obsługa tabletu ma być możliwa bez dotykania go: kursor, kliknięcia, scroll i skróty
są wysyłane z komputera i obsługiwane myszą.

Docelowo to samo urządzenie ma też sterować sprzętem na podczerwień (klimatyzacja, TV itp.).

### Główne wymagania
- Okno „touchpada” na Windowsie: ruch myszy z wciśniętym LPM = ruch kursora na iPadzie, klik = tap.
- **Zero instalacji na komputerze służbowym**: brak sterowników, brak .exe, brak Pythona.
- Prostota i stabilność ponad bogactwo funkcji.
- Bez lutowania.
- Interfejs dostępny: duże przyciski, możliwość sterowania samą myszą.

---

## 2. Kluczowe decyzje techniczne (i dlaczego)

| Decyzja | Uzasadnienie |
|---|---|
| Windows **nie** emuluje myszy BT sam | Stos BT Windows nie wspiera roli HID device (klasyczny BT), a usługa HID over GATT (0x1812) jest zarezerwowana przez system — nie da się jej wystawić z GattServiceProvider. |
| Sprzętowy mostek ESP32-S3 | ESP32-S3 udaje mysz/klawiaturę **BLE HID**; iPadOS (13.4+) natywnie obsługuje myszy BT. |
| Urządzenie: **M5Stack M5StickS3 (K150)** | Natywny USB (bez sterowników), BLE 5, obudowa, ekran, bateria, **nadajnik + odbiornik IR** (nauka kodów pilotów). |
| PC ↔ urządzenie: **USB CDC (port COM)** | Windows 10/11 ma wbudowany sterownik usbser.sys — działa bez admina. |
| Aplikacja PC: **strona HTML + Web Serial API** (Edge/Chrome) | Nic nie trzeba instalować; omija AppLocker/blokady .exe. |
| Ruch **względny** (nie absolutny) | iPadOS stosuje akcelerację kursora — mapowanie 1:1 punktów jest niemożliwe; model touchpada pasuje idealnie. |
| Odrzucone: Raspberry Pi | Droższe, wymaga sieci (blokowanej na sprzęcie służbowym) lub sterowników RNDIS; BlueZ HID kapryśny. |
| Odrzucone: M5StickC Plus2 / klasyczny ESP32 | Mostek USB-UART (CH9102/CH340/CP2102) może wymagać sterownika → problem bez admina. |

---

## 3. Sprzęt

**M5Stack M5StickS3 (SKU K150)**
- SoC: ESP32-S3-PICO-1-N8R8, dual-core Xtensa LX7 @ 240 MHz
- 8 MB Flash, 8 MB PSRAM (Octal)
- Wi-Fi 2.4 GHz + **Bluetooth 5 LE** (w układzie, choć sklepy go nie wymieniają)
- LCD 1.14", IMU 6-osiowe, programowalne przyciski
- Audio: ES8311 + mikrofon MEMS + wzmacniacz AW8737
- **IR TX + IR RX** (odbiór wymaga peryferium RMT, nie GPIO)
- Bateria 250 mAh, magnetyczny tył, Grove/HAT
- Wymiary: 48 × 24 × 15 mm
- Dokumentacja: https://docs.m5stack.com/en/core/StickS3
- Konfiguracja PlatformIO wg producenta: `board = esp32-s3-devkitc-1`, flaga `-mfix-esp32-psram-cache-issue`

Opcjonalnie później: **M5Stack Unit IR** (Grove) — większy zasięg nadawania IR.

---

## 4. Architektura

```
┌─────────────────────────┐   USB-C (CDC, port COM)   ┌──────────────────┐   BLE HID    ┌──────┐
│ Windows: Edge/Chrome     │ ───────────────────────▶ │   M5StickS3      │ ──────────▶ │ iPad │
│ touchpad.html (Web Serial)│ ◀─────────────────────── │  (firmware)      │             └──────┘
└─────────────────────────┘     statusy / ACK          │                  │   IR        ┌───────────┐
                                                       │                  │ ──────────▶ │ Klima / TV│
                                                       └──────────────────┘             └───────────┘
                                                              ▲ Wi-Fi (etap 4)
                                                              │ strona www / Skróty Siri
                                                         iPhone / iPad
```

### Komponenty
1. **firmware/** — Arduino (PlatformIO), C++
   - BLE HID composite: mysz (ruch, LPM/PPM, scroll) + klawiatura (skróty iPadOS)
   - Parser komend z USB CDC
   - Ekran: stan połączeń (PC / iPad), ostatnia akcja
   - (etap 3) IR: nauka i wysyłanie kodów, zapis w NVS/LittleFS
   - (etap 4) Wi-Fi: serwer HTTP z przyciskami
2. **web/touchpad.html** — jeden plik HTML/JS/CSS, bez zależności zewnętrznych
   - Połączenie przez `navigator.serial`
   - Pole touchpada (pointer events + pointer lock opcjonalnie)
   - Przyciski skrótów, regulacja czułości
   - Zapamiętywanie ustawień w localStorage (opakowane w try/catch)

---

## 5. Protokół szeregowy (propozycja v1)

Tekstowy, linia zakończona `\n`, 115200 baud (dla USB CDC prędkość jest umowna).

### PC → urządzenie
| Komenda | Znaczenie |
|---|---|
| `M <dx> <dy>` | Ruch względny kursora (int, zakres -127..127; większe wartości dzielić po stronie PC) |
| `D <L\|R\|M>` | Wciśnięcie przycisku myszy (przytrzymanie — do przeciągania) |
| `U <L\|R\|M>` | Zwolnienie przycisku |
| `C <L\|R\|M>` | Kliknięcie (down+up) |
| `S <v> <h>` | Scroll pionowy / poziomy |
| `K <nazwa>` | Skrót: `HOME`, `APPSWITCH`, `LASTAPP`, `BACK`, `NOTIF`, `SEARCH`, `ASSIST`, `SCREENSHOT`, `LOCK`, `VOLUP`, `VOLDOWN`, `MUTE`, `PLAY`, `BRIUP`, `BRIDOWN`, `ESC` (+ `CONTROL` w iPadOS); mapowanie zależy od profilu, lista w `profile.cpp` |
| `KC <hex> [ms]` | Diagnostyka: dowolny kod consumer, opcjonalnie przytrzymany (np. `KC 019E 1000`). Testować na odblokowanym iPadzie: wybudzony ekran blokady sam gaśnie po ~10 s |
| `KK <mods hex> <usage hex>` | Diagnostyka: dowolna kombinacja klawiszy (np. `KK 08 16` = Meta+S) |
| `P [IPAD\|ANDROID]` | Zmiana profilu (osobny adres i nazwa BLE, osobne parowanie); bez argumentu — odczyt |
| `T <tekst>` | Wpisanie tekstu na iPadzie (klawiatura BLE) |
| `SCREEN [s]` | Czas wygaszania ekranu (0 = zawsze włączony); bez argumentu — odczyt |
| `AUTOOFF [0\|1]` | Usypianie po utracie zasilania USB (karencja 30 s, sprawdzanie co 60 s) |
| `POWER` | Diagnostyka: `POWER VBUS <mV> BAT <mV> AUTOOFF <0\|1> BOOTVBUS <mV> WAKE <n> UP <s>` |
| `IR LEARN <nazwa>` | (etap 3) Nauka kodu z pilota |
| `IR SEND <nazwa>` | (etap 3) Wysłanie zapamiętanego kodu |
| `IR LIST` | (etap 3) Lista zapisanych kodów |
| `BLE` | Diagnostyka: `BLE ADDR <adres> <PUB\|RND> OK <0\|1> ADV <0\|1> PEERS <n> HOST <0\|1> HOSTON <0\|1>` + `BLE PEER <adres> ENC <0\|1> BOND <0\|1>`; wysyłane też po `P` |
| `CFG <profil> [tekst]` | Ustawienia strony per profil w NVS (`sens=1.5 scroll=1 swipe=40 invert=0`); bez tekstu — odczyt `CFG <profil> <tekst>`, pusty = brak (strona wysyła swoje) |
| `HOST [ON\|OFF]` | OFF: rozłącza hosta i przestaje się ogłaszać (host zachowuje auto-połączenie). Wraca na ON przy zmianie profilu lub gdy brak PC (USB i BT) |
| `AUTH <pin>` | Logowanie klienta Bluetooth (NUS); do tego czasu jego zapisy są odrzucane i nie dostaje powiadomień. USB nie wymaga |
| `PIN [NEW \| SET <6 cyfr>]` | Odczyt, losowy albo własny PIN (NVS); nowy obowiązuje od kolejnych połączeń |
| `PING` | Test połączenia |

### Urządzenie → PC
| Odpowiedź | Znaczenie |
|---|---|
| `PONG` | Odpowiedź na PING |
| `STATUS BLE <0\|1>` | Czy host (iPad/telefon) jest połączony |
| `STATUS USB <0\|1>` | Zmiana zasilania USB (VBUS) |
| `PROFILE <id>` | Aktywny profil (po `PING`, `P` i przycisku B) |
| `OK <komenda>` / `ERR <opis>` | Potwierdzenia (nie dla `M` — za dużo ruchu) |
| `IR LEARNED <nazwa>` | Kod zapisany |

### Transport Bluetooth (bez kabla do PC)
- Ten sam protokół przez Nordic UART Service (`6E400001-…`, RX `…0002` write, TX `…0003` notify) — `remote.cpp`.
- Strona: przycisk „Bluetooth” (Web Bluetooth); StickS3 może być wtedy zasilany z dowolnego USB.
- Host HID = połączenie szyfrowane (sparowane); klient Web Bluetooth się nie paruje (parowanie systemowe zrobiłoby z PC/telefonu hosta HID), zamiast tego `AUTH <pin>`. Odpowiedzi idą tylko do zalogowanych. Max 3 połączenia (host + PC + zapas na zawieszone łącze), reklamowanie trwa do trzeciego.
- Odpowiedzi idą do źródła komendy; `STATUS BLE/USB` i `PROFILE` (przycisk B) do obu.
- Tekst przez BLE: kolejny `T`/`K` dopiero po `OK` (brak sterowania przepływem).
- Zmiana profilu zmienia adres BLE: bieżące połączenie z PC zostaje, ale po zerwaniu trzeba wybrać urządzenie ponownie.

### Zasady
- Ruch myszy agregować po stronie przeglądarki i wysyłać max ~100 Hz (co ~10 ms).
- Firmware nie blokuje pętli przy braku połączenia BLE — odrzuca ruch, zgłasza `STATUS BLE 0`.

---

## 6. Skróty iPadOS (do zmapowania w firmware)

| Akcja | Klawisze (klawiatura BLE) |
|---|---|
| Ekran główny | Globe+H (Cmd+H nie działa w przełączniku aplikacji) |
| Przełącznik aplikacji | Globe+↑ — działa; Cmd+Tab = tylko poprzednia aplikacja |
| Spotlight | Cmd+Spacja |
| Zrzut ekranu | Cmd+Shift+3 |
| Wstecz (w wielu aplikacjach) | Cmd+[ |

> Klawisz Globe w HID: bit w bajcie 1 raportu klawiatury (Apple, page 0xFF usage 0x03) + consumer 0x029D, oba wysyłane razem — patrz `hid::key()`. Alternatywa z myszą: przesunięcie kursora do dolnej krawędzi = pasek Home.

---

## 7. Plan prac (etapy)

- [x] **Etap 0 — przygotowanie**
  - [x] M5StickS3: port COM bez sterownika (PC prywatny, Win 11)
  - [x] Test portu COM i Web Serial na PC służbowym (polityki IT) — działa, uruchamianie skrótem `--app` z menu Start
  - [x] PlatformIO (pip --user) na komputerze prywatnym
- [x] **Etap 1 — mysz dla iPada (MVP)**
  - [x] Firmware: BLE mouse + parser `M/C/D/U/S/PING`
  - [x] Ekran: status BLE i USB
  - [x] `touchpad.html`: połączenie, pole touchpada, klik, PPM, scroll (pion + poziom)
  - [x] Test parowania z iPadem i ponownego połączenia
- [ ] **Etap 2 — klawiatura i skróty**
  - [x] BLE composite (mysz + klawiatura + consumer)
  - [x] Przyciski skrótów w UI, wpisywanie tekstu `T` (wyślij / na żywo)
  - [x] Profile iPad / Android (osobne adresy BLE, przełączanie bez rozparowania)
  - [ ] Weryfikacja skrótów oznaczonych (?) w `profile.cpp`
  - [ ] Polskie znaki w `T` — zrobione (Option/AltGr+litera, układ „Polski – programisty”), do weryfikacji na iPadzie
  - [x] Wklejanie ze schowka PC, status baterii/zasilania na stronie
- [ ] **Pilot z telefonu (Android, PWA)** — `touchpad.html` z gestami dotykowymi, manifest i ikony; hosting na GitHub Pages (Web Bluetooth wymaga https). Do przetestowania na telefonie
- [ ] **Instalator dla nowych urządzeń** — `web/install.html` gotowy; do przetestowania na działającym i na fabrycznym StickS3
- [ ] **Etap 3 — podczerwień**
  - [ ] Nauka kodów (IRremoteESP8266, RMT), zapis w pamięci
  - [ ] Wysyłanie kodów, obsługa protokołu klimatyzacji (ustawianie temperatury/trybu)
  - [ ] Zakładka „Pilot” w UI
- [ ] **Etap 4 — Wi-Fi i Siri, sterowanie z telefonu** — plan: `TASK-sterowanie-z-telefonu.md`
  - [ ] Wariant A: Wi-Fi + WebSocket (strona serwowana przez StickS3)
  - [ ] Wariant B: Bluetooth z telefonu (GATT + Web Bluetooth)
  - [ ] Serwer HTTP na StickS3 z dużymi przyciskami
  - [ ] Skróty iOS wywołujące endpointy (sterowanie głosem)
  - [ ] Weryfikacja współpracy BLE + Wi-Fi (koegzystencja, płynność kursora)
- [ ] **Pomysły na później**
  - [ ] Profil iPhone (osobna tożsamość BLE, mysz przez AssistiveTouch) — plan: `TASK-profil-iphone.md`
  - [ ] Mysz sterowana ruchem głowy (IMU StickS3 zamocowany na opasce/czapce)
  - [ ] Duże przyciski dostępności podłączone przez Grove

---

## 8. Biblioteki (do weryfikacji przy starcie)

- **M5Unified** — ekran, przyciski, zasilanie StickS3
- **NimBLE-Arduino** + biblioteka HID na NimBLE (np. *BleCompositeHID* lub fork *ESP32-BLE-Combo* na NimBLE).
  Uwaga: oryginalne `ESP32-BLE-Mouse` / `BleCombo` (T-vK) mają problemy z Arduino-ESP32 core 3.x — sprawdzić aktualny stan.
- **IRremoteESP8266** — nauka/wysyłanie IR, protokoły klimatyzacji (odbiór przez RMT)
- Arduino-ESP32 core: ustawienie **USB CDC On Boot = Enabled** (port COM przez natywny USB)

---

## 9. Ograniczenia i ryzyka

- **Polityki IT**: firma może blokować nieznane urządzenia USB lub Web Serial w przeglądarce. Sprawdzić w etapie 0. Upewnić się, że regulamin pozwala podłączać własny sprzęt.
- **Web Serial wymaga bezpiecznego kontekstu** (https lub localhost). Do sprawdzenia, czy plik otwarty lokalnie (`file://`) działa w Edge; alternatywa: strona hostowana przez https.
- **Podgląd ekranu iPada na PC** (odbiornik AirPlay) raczej niemożliwy na sprzęcie służbowym — iPad stoi obok monitora.
- **Zasięg IR** wbudowanej diody: ok. 1–3 m, wymagana widoczność. Urządzenie przy PC może nie „widzieć” klimatyzatora → drugi moduł IR przez Wi-Fi lub Unit IR.
- **Akceleracja kursora iPadOS**: regulować w Ustawienia → Dostępność → Sterowanie wskaźnikiem + czułość w UI.

---

## 10. Do ustalenia

- [ ] Marka i model klimatyzacji (czy protokół jest w IRremoteESP8266)
- [ ] Lista pozostałych urządzeń IR
- [ ] Najwygodniejsze metody sterowania (mysz, głos, przyciski, ruch głowy)
- [ ] Wersja iPadOS i model iPada

---

## 11. Proponowana struktura repozytorium

```
ipad-bridge/
├── CLAUDE.md            ← ten plik
├── firmware/
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp
│       ├── protocol.cpp/.h   (parser komend)
│       ├── hid.cpp/.h        (BLE mysz + klawiatura)
│       ├── display.cpp/.h    (ekran statusu, wygaszanie)
│       ├── power.cpp/.h      (VBUS, deep sleep bez USB)
│       └── ir.cpp/.h         (etap 3)
├── tools/
│   ├── build-installer.py      (pio run + osadzenie obrazów → web/install.html)
│   ├── install.template.html   (szablon instalatora; edytować ten, nie wynik)
│   └── vendor/                 (esptool-js 0.7.0, Apache-2.0)
└── web/
    ├── touchpad.html
    └── install.html            (generowany: instalator firmware przez Web Serial, działa z file://)
```
