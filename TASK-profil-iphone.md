# Zadanie: profil iPhone

> Status: **plan, do realizacji później**. Kontekst: `CLAUDE.md`.

## Dlaczego osobny profil, a nie wspólny „iOS”

- Wspólna tożsamość BLE: iPad i iPhone łączą się naraz → drugi zajmuje miejsce pilota z PC (`MAX_PEERS = 2`), a `hostHandle` = ostatni szyfrowany.
- Skróty Globe są z iPadOS; na iPhonie potwierdzone tylko Cmd+H i Cmd+Spacja.
- Nazwa `iPad Bridge` zostaje — iPad jest z nią sparowany.

## iPhone — ograniczenia

- Kursor tylko przez **AssistiveTouch**: Ustawienia → Dostępność → Dotyk → AssistiveTouch → Urządzenia. Jednorazowa konfiguracja na telefonie; włączanie AssistiveTouch potem przez Siri.
- Prawy klik domyślnie = menu AssistiveTouch.

## Kroki

- [ ] `profile.cpp`: dopisać **na końcu** `PROFILES` (indeks = tożsamość BLE, zapisany w NVS):
  `{"IPHONE", "iPhone", "iPhone Bridge", IPHONE_KEYS, COUNT(IPHONE_KEYS), MOD_ALT}`
- [ ] `IPHONE_KEYS`:

  | Skrót | Klawisze | Status |
  |---|---|---|
  | HOME | Cmd+H | potwierdzone |
  | SEARCH | Cmd+Spacja | potwierdzone |
  | APPSWITCH | Globe+↑ | ? |
  | LASTAPP | Cmd+Tab | ? |
  | NOTIF / CONTROL / ASSIST | Globe+N / C / S | ? |
  | SCREENSHOT | Cmd+Shift+3 | ? |
  | BACK | Cmd+[ | ? |

- [ ] `touchpad.html`: profil `IPHONE` (ikona `phone`, `rightHint` „Prawy klik = menu AssistiveTouch”), `namePrefix: "iPhone Bridge"` w filtrze Bluetooth.
- [ ] Nagłówek: trzeci przycisk profilu nie zmieści się w jednej linii — zmniejszyć `.profiles button` (min-width) lub zamienić na listę.
- [ ] Test na iPhonie: parowanie przez AssistiveTouch, kursor, scroll, skróty (?).
