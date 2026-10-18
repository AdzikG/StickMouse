# Zadanie: sterowanie tabletem z telefonu

> Dokument zadania dla Claude Code. Kontekst projektu: `CLAUDE.md`.
> Status: **plan, do realizacji później**. Przed startem ustalić: model telefonu (iPhone / Android) i miejsce użycia (dom / praca).

## Cel

Sterowanie iPadem z telefonu (przeglądarka lub aplikacja), gdy StickS3 jest zasilany z ładowarki USB albo z laptopa.
Ten sam touchpad i przyciski co w `web/touchpad.html`, bez komputera.

## Wspólne założenia

- Zasilanie z ładowarki = VBUS obecny → `AUTOOFF` nie usypia; komputer niepotrzebny.
- Komendy z telefonu idą do tego samego `protocol::handle()` co z USB — USB i telefon działają równolegle.
- Odpowiedzi (`STATUS BLE`, `PROFILE`, `OK/ERR`) muszą trafiać do źródła komendy (dziś `Print& out` = `Serial`).
- Strona: ten sam kod UI, warstwa transportu wymienna (Web Serial / WebSocket / Web Bluetooth).
- Konfiguracja wyłącznie z PC lub strony, bez przycisków urządzenia.
- Kryterium: płynność kursora jak przez USB.

---

## Wariant A — Wi-Fi (StickS3 serwuje stronę)

**Jak:** StickS3 łączy się z Wi-Fi, serwuje stronę i przyjmuje komendy przez WebSocket. Telefon otwiera `http://ipad-bridge.local` (mDNS) lub IP z ekranu urządzenia.

| Plusy | Minusy |
|---|---|
| iPhone i Android, dowolna przeglądarka, zero instalacji | Wymaga wspólnej sieci Wi-Fi (dom OK; w pracy zwykle nie) |
| Ta sama strona co na PC | Koegzystencja Wi-Fi + BLE na jednym radiu — ryzyko szarpania kursora |

Kroki:
- [ ] Komenda `WIFI <ssid> <hasło>` (z PC/strony), zapis w NVS; `WIFI` = status + IP
- [ ] Serwer HTTP (strona z LittleFS lub wbudowana w firmware) + WebSocket dla komend
- [ ] mDNS `ipad-bridge.local`; IP na ekranie StickS3
- [ ] Strona: transport WebSocket gdy otwarta z urządzenia, Web Serial gdy z pliku
- [ ] Test koegzystencji: płynność kursora z włączonym Wi-Fi (ew. `WiFi.setSleep(false)` / modem sleep, parametry połączenia BLE)
- [ ] Zabezpieczenie: prosty PIN/token w URL — każdy w sieci mógłby sterować iPadem
- [ ] (opcjonalnie) Tryb AP gdy brak sieci (telefon traci wtedy internet) lub hotspot z telefonu
- [ ] (później) Skróty iOS / Siri wywołujące endpointy HTTP — patrz Etap 4

## Wariant B — Bluetooth bezpośrednio z telefonu

**Jak:** StickS3 wystawia własną usługę GATT (np. Nordic UART Service) obok HID. Telefon łączy się przez Web Bluetooth i wysyła te same komendy tekstowe.

| Plusy | Minusy |
|---|---|
| Bez sieci — działa wszędzie, także w pracy | Web Bluetooth tylko Chrome/Android; iPhone wymaga Bluefy lub własnej aplikacji |
| Brak problemu koegzystencji Wi-Fi | Druga jednoczesna rola BLE (iPad HID + telefon) — do sprawdzenia stabilność |
| | Strona musi być na https (np. GitHub Pages) |

Kroki:
- [x] Usługa GATT „komendy” (write without response) + notify dla odpowiedzi
- [x] Zezwolić na 2 połączenia (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS`), advertising dalej po połączeniu iPada
- [x] Odróżnić telefon-pilota od hosta HID (telefon nie powinien dostawać raportów myszy; `hid::connected()` = host HID, nie dowolne połączenie)
- [x] Strona: transport Web Bluetooth (`navigator.bluetooth`) — działa z PC (Edge/Chrome); dla telefonu hosting przez https
- [ ] Test z PC: płynność kursora, stabilność iPad + PC naraz
- [ ] Zabezpieczenie: usługa bez parowania — każdy w zasięgu z Web Bluetooth może wysyłać komendy
- [ ] Test: profil ANDROID (telefon jako host HID) a ten sam telefon jako pilot — wykluczyć konflikt

---

## Rekomendacja

- iPhone lub użycie głównie w domu → **A (Wi-Fi)**.
- Android i użycie także w pracy → **B (Bluetooth)**.
- Oba warianty dzielą wymienny transport w stronie i routing odpowiedzi w firmware — zrobić to raz, na początku.
