# Änderungen

## PixelUhr 1.0.0 (Firmware 1.0.0, Weboberfläche 1.0.0)

Erste Version von PixelUhr auf Basis von PixelClock 0.1.22 (Firmware) / 0.1.21 (Weboberfläche).
**Erfordert eine Erstinstallation per USB** (neue Partitionstabelle).

### Umbenennung

- Projekt, Weboberfläche, Dokumentation, Skripte und Release-Dateien heißen jetzt **PixelUhr**.
- Setup-WLAN `PixelUhr-Setup-XXXXXX` (offen, ohne Passwort), Adresse `http://pixeluhr.local`,
  Router-Name `pixeluhr-XXXXXX`, Werkszugang `admin` / `pixeluhr`.
- Versionsmarker `PIXELUHR_FIRMWARE_VERSION=` und `PIXELUHR_LITTLEFS_VERSION=`.
- Sicherungsdateien heißen `pixeluhr-settings`; alte `pixel-clock-settings` werden weiterhin importiert.

### Neu

- Frei konfigurierbarer Seitenablauf mit 15 Seitentypen und eigener Dauer je Seite.
- Lauftexte mit Farben, Regenbogen, Schrift, Geschwindigkeit, Richtung, Wiederholungen, Pause, Anzeigedauer,
  Zeitplan (Wochentage, Uhrzeit, Datum, automatisches Ende), dauerhafter Anzeige, Platzhaltern und
  „Auf der Uhr testen“.
- Termine und Kalender: sechs Terminarten, Wiederholungen mit Intervall, Vorlauf, Farbe, eigener Lauftext,
  „nur im Kalender“, Vorschau „Demnächst“ und Vorlagen. Countdown-Texte „Noch X Tage“, „Morgen“, „Heute“.
- Deutsche Feiertage je Bundesland inklusive Ostern-Berechnung, optional Gedenk- und Aktionstage.
- Wetterdetails: gefühlte Temperatur, Regenwahrscheinlichkeit, Wind, Böen, Luftfeuchtigkeit für alle fünf Anbieter
  (fehlende gefühlte Temperatur wird aus Windchill bzw. Hitzeindex berechnet).
- Neue Wettersymbole: Starkregen, Schneeregen, Wind.
- Wetterwarnungen für Frost, Hitze, Sturm und Gewitter.
- Wecker (6), Summer-Unterstützung (aktiver Piezo, Pin und Pegel einstellbar), Schlummern, Stopp per BOOT-Taste.
- Stündlicher Hinweis mit Zeitfenster.
- Nachrichten-Schlagzeilen aus RSS/Atom, Spruch des Tages, Begrüßung, Kalenderwoche, Sonnenzeiten, Mondphase,
  WLAN-Signal, vier Animationen, Konfetti am Geburtstag.
- Nachtmodus „von Sonnenuntergang bis Sonnenaufgang“ (lokal berechnet).
- Vollständige 5×7-Schrift mit Klein- und Großbuchstaben, Umlauten, ß, €, °; die 3×5-Schrift hat jetzt alle Buchstaben.
- Weboberfläche: neue Menüpunkte Lauftext, Termine, Wecker, Extras und Update; Live-Ansicht auch auf der
  Lauftext-Seite; Wetterdetails und anstehende Termine auf der Übersicht; Export/Import inklusive aller Nutzerdaten.
- **Weboberfläche fest in der Firmware** (gzip-komprimiert in `src/web_assets.h`, erzeugt von `tools/embed_web.py`):
  eine einzige Datei für Installation und Updates, kein LittleFS und kein Upload-Plugin mehr nötig.
- Arduino-IDE-Unterstützung (`PixelUhr.ino`), kompatibel mit ESP32-Core 2.0.17 und 3.x (mbedTLS 2 und 3).

### Verbessert

- Partitionstabelle ohne LittleFS: Firmware-Slots von 1,44 auf 1,94 MB vergrößert, eigener 64-KB-Bereich
  `userdata` für Nutzerdaten (übersteht Firmware-Updates).
- Separates Weboberflächen-Update entfällt; die Oberfläche wird mit jedem Firmware-Update aktualisiert.
- Eingebettete Dateien werden mit ETag ausgeliefert (unveränderte Dateien lädt der Browser nicht erneut).
- Flüssiges Scrollen: 25-ms-Bildrate nur während Lauftexte oder Animationen laufen, sonst wie bisher 1 s.
- Seitenindikator passt sich der Anzahl der Seiten an.
- Gleichzeitige Update-Uploads werden abgewiesen.
- Ungenutzte Zeichenfunktionen in `display.cpp` entfernt.

### Geänderte Dateien

`src/app_state.h`, `src/app_state.cpp`, `src/config.cpp`, `src/display.cpp`, `src/main.cpp`,
`src/maintenance.cpp`, `src/network_time.cpp`, `src/network_worker.cpp`, `src/weather.cpp`,
`src/weather_data.h`, `src/weather_decode.h`, `src/weather_icons.h`, `src/web_api.cpp`, `src/web_updates.cpp`,
`src/admin_auth.cpp`, `data/index.html`, `data/app.js`, `data/app.css`, `data/i18n.js`, `data/setup.js`,
`data/updates.js`, `partitions.csv`, `platformio.ini`, `tests/firmware-tests.cpp`,
`tests/ui.test.cjs`, `tests/mock-server.mjs`, `.github/workflows/*.yml`, `tools/*.ps1`, `*.cmd`, `README*.md`,
`docs/*.md`, `CONTRIBUTING.md`, `LICENSE`.

### Neue Dateien

`PixelUhr.ino`, `src/features.h`, `src/features.cpp`, `src/pages.cpp`, `src/alarm.cpp`, `src/news.cpp`,
`src/display_internal.h`, `src/text_layout.h`, `src/font5x7.h`, `src/calendar_logic.h`, `src/rss_parse.h`,
`src/web_assets.h` (generiert), `data/features.js`, `tools/mkfont.py`, `tools/embed_web.py`,
`scripts/embed_web_pio.py`, `LICENSE-FONT.txt`, `CHANGELOG.md`.

### Entfernte Dateien

`scripts/prepare_web.py` (LittleFS-Staging), die Weboberflächen-Upload-Route `/api/update/web`.

### Offene Punkte

- Test auf echter Hardware (Matrix, Summer, WLAN, OTA) steht aus. Kompiliert ist alles mit Core 2.0.17 und 3.3.9
  (1,43 MB bzw. 1,61 MB von 1,94 MB),
  die nativen Logik-Tests und die UI-Tests laufen durch.
- Feiertage nur für Deutschland. Schulferien sind bewusst nicht enthalten.
