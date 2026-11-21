# PixelUhr

[Deutsch](README.de.md) | English

ESP32 clock for WS2812B/NeoPixel LED matrices (default 32×8) with a modern web interface in German
and English: time, date, weather with pixel icons, scrolling texts, calendar events with countdowns,
German public holidays, alarms with a buzzer, news headlines and more.

Firmware `1.0.3` · Web interface `1.0.0` · Based on [PixelClock](https://github.com/mBlinkii/PixelClock) (MIT).

The full documentation is in German: [README.de.md](README.de.md). Short summary:

## Features

- Free page sequence (15 page types) with a duration per page; empty pages are skipped
- Up to 12 scrolling texts with colour, background, rainbow, font, speed, direction, repetitions,
  pause, display time, weekday/time/date schedule, automatic end and a "permanent" mode;
  placeholders `{time}`, `{date}`, `{weekday}`, `{temp}`, `{week}`, `{name}`, `{days:2026-12-24}`
- Up to 40 events (appointment, birthday with age, waste collection, countdown, vacation, reminder),
  repeating yearly/monthly/weekly with intervals: "30 days until Christmas", "Tomorrow is Christmas!"
- German public holidays per state, optional observances (Christmas Eve, Advent, Mother's Day …)
- Weather details (feels like, low/high, chance of rain, wind and gusts, humidity), 14 pixel icons,
  frost/heat/storm/thunderstorm warnings
- Up to 6 alarms with buzzer, LED flashing and snooze; stop with the BOOT button; hourly chime
- News headlines from any RSS/Atom feed, quote of the day, greeting, calendar week, sunrise/sunset,
  moon phase, animations
- Night dimming by fixed hours or from sunset to sunrise
- Web interface embedded in the firmware: one file for installation and OTA updates; user data lives in its own NVS partition and survives updates

## Hardware

ESP32 Dev Module (4 MB), WS2812B matrix on GPIO 18, optional active 3 V piezo buzzer on GPIO 25
(+ to GPIO, − to GND; use an NPN transistor for buzzers above 12 mA).

## Installation (once, via USB, new partition table)

- **Complete image:** write `pixeluhr-komplett-v1.0.3.bin` to `0x0`
  (e.g. with [ESP Tool](https://espressif.github.io/esptool-js/) or `esptool.py write_flash 0x0 …`).
- **Arduino IDE:** esp32 core 2.0.17 or 3.x, libraries FastLED, ArduinoJson 7, Async TCP and
  ESP Async WebServer (both by ESP32Async). Board "ESP32 Dev Module", Partition Scheme
  "Minimal SPIFFS (1.9MB APP with OTA)" (only the size check; `partitions.csv` defines the real layout).
  Upload, done. After changing `data/`, run `python3 tools/embed_web.py` before uploading.
- **PlatformIO:** `pio run -t erase`, `pio run -t upload`.

Then join the open Wi-Fi `PixelUhr-Setup-XXXXXX`, follow the assistant and open `http://pixeluhr.local`.
Log in as `admin` with an empty password; a password can be set or removed under Wi-Fi & access.

## License

MIT, see [LICENSE](LICENSE). 5×7 font from Adafruit-GFX (BSD, see [LICENSE-FONT.txt](LICENSE-FONT.txt)).
