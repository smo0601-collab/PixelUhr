#include <ArduinoJson.h>
#include <Update.h>

#include "app_state.h"
#include "web_updates.h"

String firmwareUpdateError;
bool firmwareUpdateStarted = false;
bool firmwareUpdateComplete = false;

void handleFirmwareUpdateUpload(
  AsyncWebServerRequest *request,
  String filename,
  size_t index,
  uint8_t *data,
  size_t len,
  bool final) {
  if (!isAdminAuthorized(request)) {
    return;
  }

  if (index == 0) {
    if (Update.isRunning()) {
      firmwareUpdateError = "Es läuft bereits ein Update.";
      return;
    }
    firmwareUpdateStarted = true;
    firmwareUpdateComplete = false;
    firmwareUpdateError = "";
    String lowerFilename = filename;
    lowerFilename.toLowerCase();
    if (filename.length() == 0 || !lowerFilename.endsWith(".bin")) {
      firmwareUpdateError = "Firmware-Datei muss eine .bin-Datei sein.";
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      firmwareUpdateError = Update.errorString();
      return;
    }
  }

  if (firmwareUpdateError.length() == 0 && len > 0) {
    const size_t written = Update.write(data, len);
    if (written != len) {
      firmwareUpdateError = Update.errorString();
    }
  }

  if (final && firmwareUpdateError.length() == 0) {
    if (!Update.end(true)) {
      firmwareUpdateError = Update.errorString();
    } else {
      firmwareUpdateComplete = true;
    }
  }
}

void handleFirmwareUpdateDone(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;

  JsonDocument doc;
  if (!firmwareUpdateStarted) {
    firmwareUpdateError = "Keine Firmware-Datei empfangen.";
  } else if (!firmwareUpdateComplete && firmwareUpdateError.length() == 0) {
    firmwareUpdateError = "Firmware-Upload wurde nicht abgeschlossen.";
  }

  if (Update.hasError() || firmwareUpdateError.length() > 0) {
    Update.abort();
    doc["ok"] = false;
    doc["error"] = firmwareUpdateError.length() > 0 ? firmwareUpdateError : Update.errorString();
    String body;
    serializeJson(doc, body);
    request->send(400, "application/json", body);
    return;
  }

  doc["ok"] = true;
  doc["restart"] = true;
  String body;
  serializeJson(doc, body);
  request->send(200, "application/json", body);
  scheduleRestart(1200);
}
