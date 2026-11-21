#include <Arduino.h>
#include <WiFi.h>

#include "app_state.h"
#include "features.h"
#include "runtime_policy.h"

void setup() {
  Serial.begin(115200);
  Serial.printf("PixelUhr %s, Reset: %s\n", FIRMWARE_VERSION, resetReasonText());
  stateMutex = xSemaphoreCreateRecursiveMutex();
  if (!stateMutex) abort();
  keepFirmwareVersionBinaryMarker();
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  bootStarted = millis();
  loadConfig();
  loadUserData();
  pagesReset();
  seedTimeFromBuild();
  if (authConfigMigrationNeeded) saveConfig();
  setupFastLed();
  setupBuzzer();
  renderDisplay();

  if (!connectWifi()) {
    startSetupAp();
  }
  startMdns();
  pendingCityResolve = config.resolvedCityName != config.cityName;
  pendingTimeSync = true;
  pendingWeatherFetch = true;
  lastPageSwitch = millis();
  startNetworkWorker();
  setupServer();
}

void loop() {
  serviceSetupAp();
  serviceResetButton();
  {
    StateLock lock;
    const uint32_t now = millis();
    if (displayTest && static_cast<int32_t>(now - displayTestUntil) >= 0) displayTest = false;
    if (displayPreviewActive && deadlineReached(now, displayPreviewUntil)) endDisplayPreview(true);
    if (pendingRestart && static_cast<int32_t>(now - restartAt) >= 0) {
      if (pendingFactoryWipe) performFactoryWipe();
      ESP.restart();
    }
    serviceAlarms(now);
    if (!lastRender || now - lastRender >= displayRenderInterval()) {
      lastRender = now;
      renderDisplay();
    }
  }
  // Give the idle task and Wi-Fi power management time between display checks;
  // scrolling texts and animations need a faster frame rate.
  delay(displayRenderInterval() < 100 ? 5 : 20);
}
