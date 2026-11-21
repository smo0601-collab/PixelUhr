# PixelUhr

Deutsch | [English](README.md)

ESP32-Uhr für WS2812B/NeoPixel-LED-Matrizen (Standard 32×8) mit moderner, deutschsprachiger
Weboberfläche: Uhrzeit, Datum, Wetter mit Symbolen, frei konfigurierbare Lauftexte, Termine mit
Countdown, Feiertage, Wecker mit Summer, Nachrichten und vieles mehr.

Firmware-Version: `1.0.1` · Weboberflächen-Version: `1.0.0`

PixelUhr basiert auf [PixelClock](https://github.com/mBlinkii/PixelClock) (MIT-Lizenz) und
erweitert es um die unten beschriebenen Funktionen.

## Inhalt

- [Funktionen](#funktionen)
- [Hardware und Verkabelung](#hardware-und-verkabelung)
- [Installation (einmalig per USB)](#installation-einmalig-per-usb)
- [Erste Einrichtung](#erste-einrichtung)
- [Bedienung](#bedienung)
- [Updates über WLAN](#updates-über-wlan)
- [Speicher und Datensicherheit](#speicher-und-datensicherheit)
- [Problembehandlung](#problembehandlung)
- [Entwicklung](#entwicklung)

## Funktionen

**Anzeige**
- Uhrzeit, Datum und Wetter, drei Layouts, 12/24 h, eigene Farben und Farbverläufe
- frei sortierbarer **Seitenablauf** mit eigener Dauer je Seite; Seiten ohne Inhalt werden übersprungen
- 15 Seitentypen: Uhrzeit, Datum, Wetter, Wetterdetails, Lauftexte, Termine und Feiertage,
  Kalenderwoche, Sonnenauf- und -untergang, Mondphase, Spruch des Tages, Nachrichten,
  Begrüßung, WLAN-Signal, Wetterwarnungen, Animation
- vollständige Schrift mit Groß- und Kleinbuchstaben, Umlauten, ß, €, ° (5×7) sowie eine kleine 3×5-Schrift
- Helligkeit mit Nachtabsenkung nach Uhrzeit **oder** von Sonnenuntergang bis Sonnenaufgang

**Lauftexte**
- bis zu 12 Texte mit Farbe, Hintergrund, Regenbogen, Schrift, Geschwindigkeit, Laufrichtung,
  Wiederholungen, Pause und Anzeigedauer
- Zeitplan: Wochentage, Uhrzeitfenster, nur an einem Datum, automatisches Ende
- „Dauerhafte Anzeige“ ersetzt alle anderen Seiten, solange der Zeitplan gilt
- Platzhalter `{zeit}`, `{datum}`, `{wochentag}`, `{temp}`, `{kw}`, `{name}`, `{tage:2026-12-24}`
- „Auf der Uhr testen“ zeigt einen Text sofort einmal an, ohne ihn zu speichern

**Termine und Kalender**
- bis zu 40 Termine: Termin, Geburtstag (mit Alter), Müllabfuhr, Countdown, Urlaub, Erinnerung
- Datum per Datumsauswahl, optionale Uhrzeit, Wiederholung einmalig/jährlich/monatlich/wöchentlich
  mit Intervall (z. B. Müll alle 2 Wochen)
- automatische Texte: „Noch 30 Tage bis Weihnachten“, „Morgen ist Weihnachten!“, „Heute ist Weihnachten!“
- pro Termin: anzeigen ab x Tage vorher, Farbe, eigener Lauftext, „nur im Kalender“
- deutsche Feiertage automatisch, je Bundesland, optional Heiligabend, Silvester, Advent, Nikolaus, Muttertag …
- Konfetti-Animation am Geburtstag

**Wetter**
- Open-Meteo (Standard, mit DWD-ICON-Modell wählbar), DWD/Bright Sky, MET Norway ohne API-Key;
  OpenWeatherMap und WeatherAPI mit eigenem Key
- Temperatur, gefühlte Temperatur, Tiefst- und Höchstwert, Regenwahrscheinlichkeit, Wind und Böen,
  Luftfeuchtigkeit (je nach Anbieter)
- 14 Pixelsymbole: Sonne, Mond, leicht/stark bewölkt (Tag/Nacht), Nebel, Regen, Starkregen,
  Schneeregen, Schnee, Gewitter, Wind
- Wetterwarnungen für Frost, Hitze, Sturm und Gewitter mit einstellbaren Grenzwerten

**Wecker und Hinweise**
- bis zu 6 Wecker mit Wochentagen, Bezeichnung, Summer, LED-Blinken, Klingeldauer und Schlummern
- Stopp per Weboberfläche oder kurzem Druck auf die BOOT-Taste
- stündlicher Hinweis (kurzes Aufleuchten, optional Piepen) mit Zeitfenster
- aktiver 3-V-Piezo-Summer, Pin und Logikpegel einstellbar, Test-Taste

**Extras**
- Nachrichten-Schlagzeilen aus einem RSS- oder Atom-Feed (z. B. tagesschau.de)
- Spruch des Tages (eingebaute Sprüche oder eigene Liste), Begrüßung nach Tageszeit mit Namen
- Kalenderwoche, Sonnenzeiten und Mondphase werden auf der Uhr berechnet, ganz ohne Internet
- Animationen: Regenbogen, Funkeln, Schneefall, Matrix-Regen

**System**
- Weboberfläche für Handy, Tablet und PC, Deutsch und Englisch, Hell- und Dunkelmodus
- Live-Ansicht der Matrix im Browser, Sofort-Vorschau der Anzeige-Einstellungen
- passwortgeschützt (PBKDF2-Hash, Login-Sperre), Passwort-Wiederherstellung über einen Code auf der Matrix
- Weboberfläche fest in der Firmware: nur eine Datei für Installation und Update
- Firmware-Update über WLAN mit Versionsanzeige und Fortschritt
- Export und Import aller Einstellungen inklusive Lauftexte, Termine, Wecker und Extras
- Werksreset per Weboberfläche oder BOOT-Taste (10 s)

## Hardware und Verkabelung

| Bauteil | Hinweis |
| --- | --- |
| ESP32 Dev Module | 4 MB Flash |
| WS2812B-Matrix | Standard 32×8, bis 64×16 (max. 512 LEDs) |
| Netzteil 5 V | mindestens 2 A für 32×8 mit der Standard-Helligkeitsbegrenzung, bei voller Helligkeit deutlich mehr |
| aktiver Piezo-Summer 3 V | optional, für Wecker und Stundenhinweis |

```text
ESP32                 LED-Matrix (WS2812B)
GPIO 18  ──[330 Ω, empfohlen]──► DIN
GND      ────────────► GND  ◄── GND Netzteil 5 V
                       5V   ◄── +5 V Netzteil

ESP32                 Aktiver Piezo-Summer 3 V
GPIO 25  ────────────► +  (langes Bein / rot)
GND      ────────────► −  (kurzes Bein / schwarz)
```

**Summer im Detail**
- Ein *aktiver* Summer hat einen eingebauten Oszillator und piept, sobald Spannung anliegt.
  Ein *passiver* Summer (ohne Elektronik, oft offene Unterseite) funktioniert hier nicht.
- Aktive Piezo-Summer ziehen bei 3,3 V meist nur 3–12 mA und dürfen direkt am GPIO hängen.
- Lautere Summer (über 12 mA) oder magnetische Summer über einen NPN-Transistor schalten:

  ```text
  GPIO 25 ──[1 kΩ]── Basis  BC547 / 2N2222
                     Emitter ── GND
                     Kollektor ── Summer −
  3V3 ─────────────────────────── Summer +
  ```

- Summer-Module mit drei Pins (VCC, I/O, GND): VCC an 3V3, GND an GND, I/O an GPIO 25.
  Piept das Modul ständig und ist beim Test still, unter **Hardware** „Summer schaltet bei LOW“ aktivieren.
- Erlaubte Summer-Pins: 4, 5, 13, 14, 16, 17, 19, 21, 22, 23, 25 (Standard), 26, 27, 32, 33.
  GPIO 0, 2, 12 und 15 sind Boot-Pins und werden bewusst nicht angeboten.

Wichtig: Größere Matrizen nicht über den 5-V-Pin des ESP32 versorgen und die Masse von Netzteil
und ESP32 verbinden.

## Installation (einmalig per USB)

PixelUhr nutzt eine **neue Partitionstabelle**: Die Weboberfläche steckt in der Firmware, es gibt keine
LittleFS-Partition mehr. Dafür sind die Firmware-Slots größer, und Termine und Lauftexte haben einen eigenen
Speicherbereich. Deshalb muss die Uhr einmal komplett per USB geflasht werden. Bisherige
Einstellungen vorher unter **System → Einstellungen sichern** exportieren; die Datei lässt sich danach
wieder importieren (auch Sicherungen der alten PixelClock).

Der Flash-Speicher:

```text
nvs       0x009000   20 KB  Einstellungen und WLAN
otadata   0x00E000    8 KB  Auswahl des Firmware-Slots
app0      0x010000 1984 KB  Firmware-Slot 1 (inklusive Weboberfläche)
app1      0x200000 1984 KB  Firmware-Slot 2 (für Updates über WLAN)
userdata  0x3F0000   64 KB  Lauftexte, Termine, Wecker, Extras
```

### Weg A: fertiges Komplett-Image (am einfachsten)

Die Datei `pixeluhr-komplett-v1.0.1.bin` enthält Bootloader, Partitionstabelle und Firmware
(mit Weboberfläche). Sie wird ab Adresse `0x0` geschrieben.

- **Im Browser (Chrome/Edge):** [ESP Tool](https://espressif.github.io/esptool-js/) öffnen, *Connect*,
  ESP32 auswählen, Adresse `0x0` und die Datei angeben, *Program*. Halte bei Verbindungsproblemen die
  BOOT-Taste beim Verbinden gedrückt.
- **Mit esptool:**

  ```bash
  esptool.py --chip esp32 --baud 460800 erase_flash
  esptool.py --chip esp32 --baud 460800 write_flash 0x0 pixeluhr-komplett-v1.0.1.bin
  ```

### Weg B: Arduino IDE (2.x)

1. **Board-Paket:** *Werkzeuge → Board → Boardverwalter*, „esp32 von Espressif Systems“ installieren.
   Kompiliert mit **2.0.17** (wie die PlatformIO-Builds) und **3.3.9**.
2. **Bibliotheken** über den Bibliotheksverwalter installieren:
   - FastLED (getestet 3.10.3)
   - ArduinoJson von Benoit Blanchon (7.x, getestet 7.4.3)
   - **Async TCP** von ESP32Async (getestet 3.5.0)
   - **ESP Async WebServer** von ESP32Async (getestet 3.12.1).
     Achtung: Nicht die veraltete Version von „me-no-dev“ verwenden.
3. Den Ordner `PixelUhr` öffnen (`PixelUhr.ino`). Der Programmcode liegt in `src/` und wird automatisch mitkompiliert.
4. Einstellungen unter *Werkzeuge*:
   - Board: **ESP32 Dev Module**
   - Flash Size: **4MB**
   - Partition Scheme: **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**. Diese Auswahl setzt nur die
     Größenprüfung der IDE (1,875 MB, passt sicher in den 1,94-MB-Slot); die tatsächliche Aufteilung kommt
     automatisch aus `partitions.csv` im Sketch-Ordner.
   - Erase All Flash Before Sketch Upload: **Enabled** (nur bei der Erstinstallation)
5. *Hochladen*. Fertig, die Weboberfläche ist bereits enthalten.

Wer Dateien in `data/` ändert, startet danach einmal `python3 tools/embed_web.py`. Das Skript erzeugt
`src/web_assets.h` neu; dann wie gewohnt hochladen.

### Weg C: PlatformIO

```bash
pio run -t erase                 # nur bei der Erstinstallation
pio run -t upload                # Firmware mit Weboberfläche (src/web_assets.h wird automatisch erzeugt)
pio device monitor               # serielle Ausgabe, 115200 Baud
```

Unter Windows gibt es dafür die Hilfsskripte `build-pixeluhr.cmd`, `flash-pixeluhr.cmd` und
`reset-pixeluhr.cmd` (Parameter siehe `-ShowHelp`).

## Erste Einrichtung

1. Die Uhr startet ohne gespeichertes WLAN im Setup-Modus. Die Matrix zeigt abwechselnd `WIFI`, `AP`
   und eine sechsstellige Kennung.
2. Am Handy das WLAN **`PixelUhr-Setup-XXXXXX`** wählen. Es ist **offen, ein Passwort ist nicht nötig**.
   Die Einrichtungsseite öffnet sich meist von selbst, sonst `http://192.168.4.1` aufrufen.
3. Der Assistent fragt WLAN, Standort und ein eigenes Admin-Passwort ab. Eine Anmeldung ist dabei nicht nötig.
4. Nach dem Neustart ist die Uhr im Heimnetz unter **`http://pixeluhr.local`** erreichbar
   (oder über die IP-Adresse aus dem Router). Benutzer `admin`, Passwort aus dem Assistenten.

Der Werkszugang `admin` / `pixeluhr` gilt nur für neue Uhren und wird als neues Passwort nicht akzeptiert.
Wer möchte, vergibt unter **WLAN & Zugang → Setup-WLAN** ein Passwort für das Setup-WLAN.

## Bedienung

| Menü | Inhalt |
| --- | --- |
| Übersicht | Uhrzeit, Wetter mit Details, Live-Ansicht der Matrix, anstehende Termine, Wecker stoppen |
| Anzeige | Layout, Farben, Helligkeit, Nachtmodus, **Seitenablauf** (Reihenfolge und Dauer) |
| Lauftext | Lauftexte anlegen, Zeitplan, „Auf der Uhr testen“ |
| Termine | Termine, Geburtstage, Müll, Countdowns, Vorlagen, Vorschau „Demnächst“, Feiertage |
| Wetter | Standort, Wetterdienst, Modell, Intervall |
| Wecker | Weckzeiten, Summer-Test, stündlicher Hinweis |
| Extras | Laufgeschwindigkeit und Farbe der Meldungen, Begrüßung, Sprüche, Nachrichten, Warnungen, Wetterdetails, Animationen |
| WLAN & Zugang | WLAN, Adresse, Admin-Passwort, Setup-WLAN |
| Hardware | Matrixgröße, Daten-Pin, Verkabelung, Testmuster, **Summer** |
| Update | Firmware und Weboberfläche über WLAN aktualisieren |
| System | Geräteinfo, Assistent, Sichern/Wiederherstellen, Hilfe, Zurücksetzen |

Anzeige-Einstellungen werden auf der Uhr sofort als Vorschau gezeigt und mit **Speichern** übernommen.
Lauftexte, Termine, Wecker und Extras haben jeweils einen eigenen Speichern-Knopf.

**Platzhalter in Lauftexten:** `{zeit}` 14:30 · `{datum}` 24.12.2026 · `{wochentag}` Donnerstag ·
`{temp}` 5°C · `{kw}` 52 · `{name}` Name aus der Begrüßung · `{tage:2026-12-24}` Tage bis zum Datum.
**In Terminen zusätzlich:** `{name}`, `{tage}`, `{datum}`, `{zeit}`, `{alter}`.

**Beispiel Weihnachts-Countdown:** Unter *Termine* die Vorlage *Weihnachten* wählen (24.12., jährlich,
30 Tage vorher). Die Uhr zeigt ab dem 24.11. „Noch 30 Tage bis Weihnachten“ … „Noch 7 Tage bis
Weihnachten“, am 23.12. „Morgen ist Weihnachten!“ und am 24.12. „Heute ist Weihnachten!“.

## Updates über WLAN

1. Unter **Update** die Firmware-Datei `pixeluhr-firmware-vX.Y.Z.bin` auswählen. Die Weboberfläche liest die
   Version aus der Datei und warnt vor älteren Versionen.
2. *Firmware aktualisieren*. Der Fortschritt wird angezeigt, danach startet die Uhr neu.
   Die Weboberfläche wird dabei automatisch mit aktualisiert.

Die Firmware wird in den jeweils freien Slot geschrieben und erst nach vollständiger, geprüfter Übertragung
aktiviert. Bricht ein Update ab, läuft die bisherige Firmware weiter. Einstellungen, Lauftexte, Termine,
Wecker und Extras bleiben erhalten. Eine automatische Updateprüfung ist bewusst nicht enthalten.

## Speicher und Datensicherheit

- Einstellungen liegen im NVS, Lauftexte, Termine, Wecker und Extras im eigenen NVS-Bereich `userdata`.
  Ein Firmware-Update löscht diese Daten nicht.
- Geschrieben wird nur beim Speichern und nur, wenn sich etwas geändert hat.
- WLAN-Passwort, Admin-Passwort und API-Keys werden nie an den Browser zurückgegeben.
- Das Admin-Passwort wird als gesalzener PBKDF2-HMAC-SHA256-Hash gespeichert.
- *Einstellungen zurücksetzen* behält WLAN, Admin-Zugang, Lauftexte, Termine und Wecker.
  *Werksreset* löscht alles, inklusive Lauftexte und Termine.
- Die Anmeldung läuft über HTTP Basic Auth. Das ist im Heimnetz praktisch, aber nicht verschlüsselt.
  Die Uhr daher nicht in fremden oder öffentlichen Netzen betreiben.
- Nachrichten-Feeds werden per HTTPS ohne Zertifikatsprüfung geladen, damit jeder Anbieter funktioniert.
  Übertragen werden nur öffentliche Schlagzeilen, keine Zugangsdaten.

## Problembehandlung

**Weboberfläche nicht erreichbar:** IP-Adresse aus dem Router statt `pixeluhr.local` verwenden. Ohne WLAN mit
`PixelUhr-Setup-XXXXXX` verbinden und `http://192.168.4.1` öffnen.

**Weboberfläche zeigt nach einer Änderung in `data/` noch den alten Stand:** `python3 tools/embed_web.py`
ausführen, neu hochladen und die Seite im Browser neu laden.

**Arduino IDE meldet „Sketch too big“:** Das Partitionsschema „Minimal SPIFFS (1.9MB APP with OTA)“ wählen
(siehe Installation, Weg B, Schritt 4).

**Kompilierfehler mit AsyncTCP/ESPAsyncWebServer:** Alte Bibliotheken von me-no-dev entfernen und die
Versionen von *ESP32Async* installieren.

**Login klappt nicht:** Auf der Login-Seite *Passwort vergessen?* wählen. Die Matrix zeigt einen 6-stelligen Code.
Notfalls die BOOT-Taste 10 Sekunden halten (Werksreset).

**Wecker stoppen:** BOOT-Taste kurz drücken, in der Weboberfläche *Stopp* oder *Schlummern* tippen, oder warten,
bis die Klingeldauer abgelaufen ist.

**Summer bleibt stumm:** Unter *Hardware* Pin prüfen, *Speichern*, dann *Summer testen*. Polung beachten (+ an den
GPIO). Bei Modulen mit Low-Level-Trigger die Option „Summer schaltet bei LOW“ aktivieren.

**Termin erscheint nicht:** Ist er aktiv, „Auf der LED-Matrix anzeigen“ eingeschaltet und liegt er innerhalb von
„Anzeigen ab … Tage vorher“? Außerdem muss die Seite *Termine und Feiertage* im Seitenablauf aktiv sein.
Unter *Termine → Demnächst auf der Uhr* ist zu sehen, was die Uhr gerade anzeigen würde.

**Nachrichten erscheinen nicht:** Feed-Adresse unter *Extras* prüfen, *Jetzt laden* tippen, Fehlermeldung beachten
und die Seite *Nachrichten* im Seitenablauf aktivieren.

**Wetter fehlt:** WLAN, Internet, Stadt und gegebenenfalls API-Key prüfen. Fehler stehen auf der Übersicht.

**LEDs dunkel / Farben falsch / Bild gespiegelt:** Helligkeit (0 % = aus), Daten-Pin (Standard GPIO 18),
gemeinsame Masse, Farbreihenfolge GRB/RGB, Start-Ecke und Verkabelung prüfen. Das *Testmuster* hilft dabei.

**Uhr startet immer wieder neu:** Unter *System* den letzten Startgrund ablesen. „Unterspannung (Brownout)“ deutet
auf ein zu schwaches Netzteil hin.

## Entwicklung

```text
PixelUhr.ino              Sketch für die Arduino IDE (Code liegt in src/)
platformio.ini            PlatformIO-Projekt
partitions.csv            Flash-Layout (wird von Arduino IDE und PlatformIO genutzt)
src/main.cpp              setup() und loop()
src/app_state.h/.cpp      gemeinsame Konfiguration, Version, Laufzeitstatus
src/config.cpp            Einstellungen laden/speichern (NVS)
src/features.h/.cpp       Datenmodell und API für Lauftexte, Termine, Wecker, Extras (userdata-NVS)
src/pages.cpp             Seitenablauf, Lauftext-Player, Info-Seiten, Symbole, Animationen
src/alarm.cpp             Wecker, Summer, stündlicher Hinweis
src/news.cpp              RSS/Atom-Schlagzeilen (Netzwerk-Task)
src/display.cpp           LED-Mapping, Uhr/Datum/Wetter-Seiten, Wettersymbol-Auswahl
src/text_layout.h         UTF-8, 5×7- und 3×5-Schrift (portabel, getestet)
src/font5x7.h             Schrifttabelle (aus Adafruit-GFX, BSD, siehe LICENSE-FONT.txt)
src/calendar_logic.h      Datum, ISO-Woche, Ostern, Feiertage, Wiederholungen, Sonne, Mond (portabel, getestet)
src/rss_parse.h           Streaming-RSS-Parser (portabel, getestet)
src/weather*.h/.cpp       Wetterdienste, Decoder, Symbole
src/web_api.cpp           HTTP-API, Auslieferung der eingebetteten Weboberfläche
src/web_assets.h          Weboberfläche als gzip-Daten (erzeugt von tools/embed_web.py, nicht von Hand ändern)
src/web_updates.cpp       OTA-Upload der Firmware
data/                     Weboberfläche (app.js, features.js, i18n.js, setup.js, updates.js, index.html, app.css)
tools/embed_web.py        bettet data/ in die Firmware ein (PlatformIO ruft es über scripts/embed_web_pio.py auf)
tests/                    native Firmware-Tests, UI-Tests, Mock-Server
```

Tests:

```bash
g++ -std=c++17 -I <Pfad zu ArduinoJson/src> tests/firmware-tests.cpp -o firmware-tests && ./firmware-tests
node --test tests/ui.test.cjs
node tests/mock-server.mjs     # Weboberfläche lokal unter http://localhost:8765 (beliebiges Passwort)
```

Details zur Architektur: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Alle Änderungen gegenüber PixelClock 0.1.22:
[CHANGELOG.md](CHANGELOG.md).

## Lizenz und Credits

MIT-Lizenz, siehe [LICENSE](LICENSE). Die 5×7-Schrift stammt aus der Adafruit-GFX-Library (BSD-Lizenz,
siehe [LICENSE-FONT.txt](LICENSE-FONT.txt)). Wetterdaten vom gewählten Dienst. MET-Norway-Daten:
[MET Norway](https://www.met.no/), CC BY 4.0. Ortsnamen über Open-Meteo Geocoding, Postleitzahlen über
OpenStreetMap Nominatim (© OpenStreetMap-Mitwirkende, ODbL). Ursprungsprojekt: PixelClock von mBlinkii.
