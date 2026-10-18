# Zadanie: zarządzanie ekranem i zasilaniem + automatyczne ponowne łączenie

> Dokument zadania dla Claude Code. Kontekst projektu: `CLAUDE.md`.
> Status: **do zrobienia**. Przed implementacją przeczytaj istniejący kod firmware i `web/touchpad.html`
> i dopasuj rozwiązanie do obecnej struktury (nie przepisuj działających części).

---

## Tło

Rozwiązanie działa (StickS3 jako mysz BLE dla iPada, sterowanie z przeglądarki przez Web Serial).
Zaobserwowane problemy w codziennym użyciu:

1. Ekran StickS3 świeci bez przerwy przez cały dzień — niepotrzebny pobór prądu i ciepło.
2. Po wyłączeniu komputera port USB zwykle traci zasilanie, a StickS3 przechodzi na wbudowany akumulator (250 mAh)
   i działa dalej, aż do rozładowania baterii do zera.
3. Nie wiadomo, czy po ponownym włączeniu komputera wszystko połączy się samo (USB/Web Serial i iPad/BLE).

---

## Zmiana 1 — wygaszanie ekranu

**Cel:** ekran gaśnie po okresie bezczynności i wybudza się przy aktywności.

- Parametr `SCREEN_TIMEOUT_MS` (domyślnie 60 000 ms), łatwy do zmiany w jednym miejscu.
- Opcjonalnie dwa stopnie: najpierw przyciemnienie (np. po 30 s), potem wyłączenie podświetlenia (po 60 s).
- **Aktywność, która resetuje licznik i wybudza ekran:**
  - każda komenda z USB (`M`, `C`, `D`, `U`, `S`, `K`, `T`, `IR …`),
  - naciśnięcie przycisku wbudowanego w StickS3 lub przycisku na Grove,
  - zmiana stanu połączenia (połączono / rozłączono iPad, USB).
- Wybudzenie nie może opóźniać obsługi komend (rysowanie ekranu poza ścieżką krytyczną ruchu myszy).
- Użyj API M5Unified (np. `M5.Display.setBrightness()`, `M5.Display.sleep()` / `M5.Display.wakeup()`) —
  sprawdź, które działają poprawnie na StickS3.
- (Opcjonalnie) komenda `SCREEN <sekundy>` do zmiany timeoutu z aplikacji, `SCREEN 0` = zawsze włączony.

---

## Zmiana 2 — wyłączenie po utracie zasilania USB

**Cel:** gdy komputer zostanie wyłączony, StickS3 nie rozładowuje baterii przez całą noc.

- Wykrywanie obecności zasilania USB (VBUS). Sprawdź w M5Unified, co działa na StickS3:
  np. `M5.Power.getVBUSVoltage()`, `M5.Power.isCharging()` lub odczyt stanu z układu zarządzania zasilaniem.
  Zweryfikuj na sprzęcie, zanim oprzesz na tym logikę.
- Po wykryciu braku zasilania USB:
  - pokaż na ekranie komunikat „Brak zasilania USB — wyłączenie za 30 s”,
  - odczekaj **okres karencji** `USB_LOSS_GRACE_MS` (domyślnie 30 000 ms),
  - jeśli zasilanie wróci w tym czasie → anuluj wyłączenie,
  - jeśli nie → wyłącz urządzenie (`M5.Power.powerOff()` lub deep sleep, jeśli powerOff nie jest wspierany).
- Upewnij się, że przed wyłączeniem zapisane są potrzebne dane (np. kody IR, ustawienia).
- **Ważne do sprawdzenia:** czy po ponownym podaniu zasilania USB StickS3 sam się włącza.
  Jeśli nie — znajdź alternatywę (np. deep sleep z wybudzeniem, gdy wróci VBUS, jeśli sprzęt na to pozwala)
  i opisz wynik w tym dokumencie.
- Gdy urządzenie pracuje z baterii celowo (np. jako pilot IR w innym miejscu), ta funkcja może przeszkadzać —
  przewidź możliwość wyłączenia jej (stała w kodzie lub komenda `AUTOOFF 0|1`).

---

## Zmiana 3 — automatyczne ponowne łączenie

### 3a. Przeglądarka (Web Serial)
- Przy załadowaniu strony: `navigator.serial.getPorts()` — jeśli jest wcześniej zatwierdzony port,
  połącz się automatycznie bez klikania.
- Obsłuż zdarzenia `navigator.serial` `connect` / `disconnect`:
  - przy odłączeniu: czytelny komunikat w UI („Urządzenie odłączone — czekam…”),
  - przy podłączeniu: automatyczne ponowne otwarcie portu.
- Po połączeniu wyślij `PING` i pokaż stan (USB / iPad) na podstawie odpowiedzi `STATUS BLE`.

### 3b. iPad (BLE)
- Sprawdź, czy parowanie (bonding) jest zapisywane w pamięci nieulotnej (NVS),
  tak aby iPad łączył się automatycznie po restarcie StickS3 bez ponownego parowania.
  W NimBLE: włączone bonding w ustawieniach bezpieczeństwa (np. `setSecurityAuth`), sprawdź, czy klucze trafiają do NVS.
- Po starcie urządzenie od razu rozpoczyna advertising.
- Ekran po starcie: „Czekam na iPada…” → „iPad połączony”.

---

## Kryteria akceptacji

- [ ] Ekran gaśnie po ustawionym czasie bezczynności i wybudza się przy ruchu myszy / kliknięciu / przycisku.
- [ ] Ruch kursora na iPadzie jest tak samo płynny jak przed zmianami.
- [ ] Po odłączeniu zasilania USB urządzenie wyłącza się po okresie karencji; krótki zanik (< 30 s) go nie wyłącza.
- [ ] Po ponownym podaniu zasilania USB urządzenie startuje samo (lub opisano, dlaczego nie i jakie jest obejście).
- [ ] Po restarcie komputera strona łączy się z urządzeniem automatycznie (bez ponownego wybierania portu),
      a iPad łączy się bez ponownego parowania.

---

## Scenariusz testu ręcznego

1. Normalna praca → odczekaj czas wygaszenia → ekran gaśnie → rusz myszą w oknie touchpada → ekran się zapala, kursor działa.
2. Wyciągnij kabel USB → komunikat na ekranie → podłącz ponownie przed upływem 30 s → urządzenie działa dalej.
3. Wyciągnij kabel USB i odczekaj > 30 s → urządzenie się wyłącza.
4. Podłącz kabel → urządzenie startuje samo → iPad łączy się bez parowania.
5. Wyłącz komputer na noc → rano włącz → otwórz stronę → połączenie automatyczne, iPad sterowany bez dodatkowych kroków.
6. Sprawdź poziom baterii rano (nie powinien spaść do zera).

---

## Notatki / wyniki (do uzupełnienia)

Status: **wdrożone** (2026-10-15), scenariusze 2–6 do potwierdzenia przy wyłączeniu PC.

- **VBUS**: `M5.Power.getVBUSVoltage()` (PM1, rej. VIN 0x24) — z USB ~4870 mV, próg 4000 mV. `isCharging()` nie nadaje się (pokazuje ładowanie, nie zasilanie).
- **Brak `powerOff()`**: wyłączenie PM1 może wymagać przycisku do ponownego startu, a urządzenie ma działać bez przycisków. Zamiast tego deep sleep ESP32 z budzeniem co 60 s: `power::earlyBoot()` czyta VIN bezpośrednio z PM1 przez I2C (przed `M5.begin()`, ekran pozostaje zgaszony) i usypia ponownie, jeśli USB nadal nie ma. Po włączeniu PC urządzenie startuje samo w ≤ 60 s.
- Zweryfikowane na sprzęcie: surowy odczyt VIN = odczyt M5Unified; budzenie z timera przy obecnym USB → normalny start + BLE (`WAKE 4`).
- Niezweryfikowane: faktyczne odliczanie 30 s po odłączeniu USB i zużycie w uśpieniu (PM1, panel LCD w SLPIN, IMU zostają zasilone). Rano sprawdzić `POWER` → `BAT`.
- Ekran: tylko podświetlenie PWM (przyciemnienie w połowie czasu, wyłączenie po `SCREEN_TIMEOUT`), bez komend do panelu — zero wpływu na ruch kursora. `PING` (co 1 s ze strony) nie liczy się jako aktywność.
- Parowanie: NimBLE zapisuje bondy w NVS (`CONFIG_BT_NIMBLE_NVS_PERSIST 1`), advertising od startu — bez zmian.
- Nowe komendy: `SCREEN [s]` (0 = zawsze), `AUTOOFF [0|1]`, `POWER`; zdarzenie `STATUS USB 0|1`. Ustawienia w NVS, dostępne w panelu „Ustawienia” strony.
