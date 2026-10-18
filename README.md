# StickMouse

Sterowanie iPadem lub telefonem z Androidem z komputera z Windowsem. M5StickS3 udaje mysz i klawiaturę Bluetooth, a komputer steruje nim ze strony w przeglądarce. Na komputerze nie trzeba niczego instalować.

```
Chrome/Edge (touchpad.html) --USB albo Bluetooth--> M5StickS3 --Bluetooth HID--> iPad / Android
```

## Uruchomienie

1. Skopiuj `web/touchpad.html` i `web/icon.ico` na komputer, np. do `Dokumenty\StickMouse\`.
2. Utwórz skrót w menu Start (bez admina). W PowerShell:
   ```powershell
   $html = "$env:USERPROFILE\Documents\StickMouse\touchpad.html"
   $b = @("$env:ProgramFiles\Google\Chrome\Application\chrome.exe", "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe", "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
   $s = (New-Object -ComObject WScript.Shell).CreateShortcut("$env:APPDATA\Microsoft\Windows\Start Menu\Programs\StickMouse.lnk")
   $s.TargetPath = $b; $s.Arguments = "--app=`"file:///$($html -replace '\\','/')`""; $s.IconLocation = "$(Split-Path $html)\icon.ico,0"; $s.Save()
   ```
   Skrót otwiera stronę w Chrome (albo w Edge) jako osobne okno, nawet gdy domyślną przeglądarką jest Firefox.
3. Podłącz StickS3 kablem i kliknij **USB**. Strona sama łączy się z ostatnim urządzeniem.
   Bez kabla do PC: kliknij **Bluetooth**. StickS3 może być wtedy zasilany z dowolnej ładowarki.

## Telefon z Androidem jako pilot

StickS3 zasilany z dowolnego USB, telefon łączy się z nim przez Bluetooth i steruje iPadem.

1. Na telefonie w Chrome otwórz stronę z GitHub Pages (adres w ustawieniach repozytorium, *Pages*).
2. Menu ⋮ → *Dodaj do ekranu głównego* — pojawi się ikona StickMouse.
3. **Bluetooth** → wybierz `iPad Bridge` i wpisz PIN (jednorazowo).

**PIN Bluetooth:** sterowanie przez Bluetooth wymaga 6-cyfrowego PIN-u, żeby nikt w zasięgu nie mógł przejąć tabletu. PIN widać w *Ustawieniach* strony połączonej przez USB (tam też *Nowy PIN*). Przez USB PIN nie jest potrzebny.

Gesty: jeden palec = kursor, stuknięcie = klik, dwa palce = przewijanie, stuknięcie dwoma = menu, przytrzymanie = przeciąganie. Ekran telefonu nie gaśnie, dopóki strona jest połączona.

## Parowanie

| Profil | Nazwa w ustawieniach Bluetooth |
|---|---|
| iPadOS | `iPad Bridge` |
| Android | `Bridge Android` |

Profil wybierasz przyciskami na górze strony. Każdy profil ma osobny adres Bluetooth, więc oba urządzenia zostają sparowane, a zmiana profilu nie wymaga ponownego parowania.

**Android:** nie rozłączaj StickS3 w ustawieniach Bluetooth telefonu, bo telefon przestanie wtedy łączyć się sam. Zamiast tego użyj *Ustawienia → Odłącz: Android*, a potem *Podłącz*.

## Strona

- **Touchpad:** przeciągnięcie z wciśniętym LPM to ruch kursora, krótki klik to tap, PPM to menu (na Androidzie: Wstecz), kółko przewija.
- **Przechwyć mysz:** cała mysz steruje urządzeniem. Wyjście: PPM albo Esc.
- **Wpisz tekst:** pole „Wyślij” albo tryb „Na żywo”. Obsługuje polskie znaki; na urządzeniu musi być układ klawiatury *Polski – programisty*.
- **Ustawienia:** czułość kursora i przewijania, motyw strony, odłączanie hosta, parowania i log. Czułość i przewijanie zapisują się w StickS3 osobno dla każdego profilu, więc działają na każdym komputerze.

## Przyciski na StickS3

| Przycisk | Działanie |
|---|---|
| B (boczny), klik | Następny profil |
| A (przedni), 3 s | Usuń wszystkie parowania |

## Problemy

| Objaw | Rozwiązanie |
|---|---|
| Telefon nie łączy się sam | Na telefonie: Bluetooth → stuknij `Bridge Android` (nie zapominaj urządzenia). |
| Na liście Bluetooth jest zła nazwa | Na stronie przełącz profil tam i z powrotem; w *Ustawieniach* przy nazwie profilu widać adres w użyciu. |
| Wgrywanie firmware: port zajęty | Zamknij okno StickMouse (strona trzyma port COM). |
| Skrót nie działa na urządzeniu | Wypróbuj kod komendą `KC <kod> [ms]` albo `KK <mod> <klawisz>` (patrz CLAUDE.md). Blokadę testuj na **odblokowanym** iPadzie: ekran blokady sam gaśnie po ok. 10 s. |

## Instalacja na nowym M5StickS3

Bez PlatformIO i Pythona, tylko Chrome albo Edge:

1. Podłącz StickS3 kablem USB-C.
2. Otwórz `web/install.html` (przeciągnij plik do okna Chrome) i kliknij **Zainstaluj**, wybierz port.
3. Po komunikacie *Gotowe* otwórz StickMouse i sparuj urządzenia.

Fabryczny StickS3 może nie przejść sam w tryb wgrywania: przytrzymaj przycisk zasilania, aż zacznie migać zielona dioda, i kliknij Zainstaluj ponownie. *Wyczyść całą pamięć* usuwa parowania i ustawienia, więc na działającym urządzeniu zostaw je odznaczone.

Po zmianach w firmware odśwież instalator (kompiluje i osadza obrazy w stronie):

```
python tools/build-installer.py
```

## Firmware

PlatformIO, katalog `firmware/`:

```
pio run -t upload --upload-port COM26
```

Jeśli płytka utknie w trybie pobierania (miga zielona dioda): dwuklik przycisku zasilania (wyłączenie), potem jeden klik (włączenie).

Szczegóły (architektura, protokół szeregowy, plan prac): [CLAUDE.md](CLAUDE.md).

## Licencja

Kod projektu: [MIT](LICENSE). Wykorzystane biblioteki:

| Biblioteka | Licencja |
|---|---|
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | Apache-2.0 |
| [M5Unified](https://github.com/m5stack/M5Unified), [M5GFX](https://github.com/m5stack/M5GFX) | MIT |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) | LGPL-2.1 (źródła firmware są w tym repozytorium) |
| [esptool-js](https://github.com/espressif/esptool-js) (w `web/install.html`) | Apache-2.0, [tekst licencji](tools/vendor/esptool-js-LICENSE) |

## Zastrzeżenia

Projekt niezależny, niezwiązany z Apple, Google, M5Stack ani Bluetooth SIG. iPad, iPadOS i Siri są znakami towarowymi Apple Inc., Android — Google LLC, Bluetooth — Bluetooth SIG, Inc.; nazwy służą wyłącznie opisowi zgodności. Oprogramowanie dostarczane jest „tak jak jest”, bez gwarancji (patrz [LICENSE](LICENSE)).
