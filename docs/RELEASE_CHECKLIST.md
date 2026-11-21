# Release Checklist

Use this before publishing a release or pushing a public branch.

## Code

- Bump `FIRMWARE_VERSION` in `src/app_state.h` for every firmware change.
- Bump `littleFsVersionMarker` in `data/updates.js` (web interface version shown in the UI) for every change in `data/`.
- Keep existing `/api/update/firmware` and `/api/update/web` routes working.
- For changed web/API contracts, keep old routes as aliases or add `/api/status` capability detection.
- Run `pio run`.
- Platform and library versions are pinned in `platformio.ini`. Update them
  deliberately, rebuild and keep the firmware below the 2,031,616-byte OTA slot
  (PixelUhr 1.0.0: about 1.43 MB with core 2.0.17, 1.61 MB with core 3.3.9).
- Run `python3 tools/embed_web.py` after every change in `data/` and commit `src/web_assets.h`.
- Also compile once in the Arduino IDE (or arduino-cli) with Partition Scheme
  "Minimal SPIFFS (1.9MB APP with OTA)"; `partitions.csv` in the sketch folder is used automatically.
- Run `g++ -std=c++17 -I <ArduinoJson/src> tests/firmware-tests.cpp` and `node --test tests/ui.test.cjs`.
- Make sure the `PlatformIO` workflow is green on the commit you tag; the
  `Release` workflow only builds and does not run the tests.
- Run `.\build-pixeluhr.cmd` when you need the two versioned upload binaries in `dist/`.
- Flash firmware with `pio run --target upload` when firmware changed.

## Documentation

- Keep `README.md` and `README.de.md` aligned.
- Keep the firmware version in the README files aligned with `FIRMWARE_VERSION`.
- Keep the LittleFS web interface version in the README files aligned with `littleFsVersionMarker`.
- Update troubleshooting notes when behavior changes.

## Security

- Do not commit real Wi-Fi credentials.
- Do not commit real OpenWeatherMap API keys.
- Confirm default login and setup AP warnings are documented.
- Confirm the admin password reminder appears only while the default admin password is active.

## Git

- Check `git status --short`.
- Check ignored files with `git status --ignored --short` if unsure.
- Avoid committing `.pio/`, generated firmware binaries, local VS Code C++ config, or upload logs.
- Push a `vX.Y.Z` tag after merging the release commit. The `Release` GitHub
  Actions workflow builds firmware and LittleFS images, creates the GitHub
  release, and uploads the versioned `.bin` files.
