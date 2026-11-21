#include "app_state.h"
#include "display_internal.h"
#include "features.h"
#include "text_layout.h"

// Alarms with LED signal and an active piezo buzzer, plus the hourly chime.
// Everything is non-blocking: loop() calls serviceAlarms() every iteration.

namespace {

enum BuzzerPattern : uint8_t { BUZZ_NONE, BUZZ_ALARM, BUZZ_CHIME, BUZZ_TEST };

struct Ring {
  bool active = false;
  uint8_t index = 0;
  uint32_t startedMs = 0;
  uint32_t durationMs = 0;
  bool buzzer = true;
  bool flash = true;
  String label;
};

Ring ring;
bool snoozePending = false;
uint8_t snoozeIndex = 0;
uint32_t snoozeAt = 0;
int32_t lastTriggerKey = -1;
int lastChimeHour = -1;
uint32_t chimeUntil = 0;
uint32_t lastCheck = 0;

BuzzerPattern pattern = BUZZ_NONE;
uint32_t patternStarted = 0;
uint8_t buzzerPinActive = 0;
bool buzzerLevel = false;

bool validBuzzerPin(uint8_t pin) {
  static const uint8_t allowed[] = {4, 5, 13, 14, 16, 17, 19, 21, 22, 23, 25, 26, 27, 32, 33};
  if (pin == config.dataPin) return false;
  for (uint8_t candidate : allowed) {
    if (candidate == pin) return true;
  }
  return false;
}

void buzzerWrite(bool on) {
  if (!buzzerPinActive || on == buzzerLevel) return;
  buzzerLevel = on;
  digitalWrite(buzzerPinActive, (on != config.buzzerActiveLow) ? HIGH : LOW);
}

void startPattern(BuzzerPattern next, uint32_t now) {
  if (!buzzerPinActive) return;
  pattern = next;
  patternStarted = now;
}

// Returns whether the buzzer should sound `elapsed` ms into a pattern; sets
// `finished` when a one-shot pattern is over.
bool patternLevel(BuzzerPattern p, uint32_t elapsed, bool &finished) {
  finished = false;
  switch (p) {
    case BUZZ_ALARM: {
      const uint32_t phase = elapsed % 1560;  // 4 x (120 on, 120 off), then 600 ms silence
      return phase < 960 && (phase % 240) < 120;
    }
    case BUZZ_CHIME:
      if (elapsed >= 400) { finished = true; return false; }
      return (elapsed % 200) < 80;
    case BUZZ_TEST:
      if (elapsed >= 1050) { finished = true; return false; }
      return (elapsed % 350) < 200;
    default:
      finished = true;
      return false;
  }
}

void startRing(uint8_t index, uint32_t now) {
  if (index >= alarms.size()) return;
  const AlarmEntry &alarm = alarms[index];
  ring.active = true;
  ring.index = index;
  ring.startedMs = now;
  ring.durationMs = alarm.durationMinutes * 60000UL;
  ring.buzzer = alarm.buzzer;
  ring.flash = alarm.flash;
  ring.label = alarm.label;
  snoozePending = false;
  if (ring.buzzer) startPattern(BUZZ_ALARM, now);
  lastRender = 0;
  Serial.printf("Alarm %u ringing\n", index + 1);
}

}  // namespace

void setupBuzzer() {
  const uint8_t next = validBuzzerPin(config.buzzerPin) ? config.buzzerPin : 0;
  if (buzzerPinActive && buzzerPinActive != next) {
    digitalWrite(buzzerPinActive, config.buzzerActiveLow ? HIGH : LOW);
    pinMode(buzzerPinActive, INPUT);
  }
  buzzerPinActive = next;
  if (!buzzerPinActive) return;
  pinMode(buzzerPinActive, OUTPUT);
  buzzerLevel = true;
  buzzerWrite(false);
}

bool alarmRinging() { return ring.active; }

bool alarmStop() {
  const bool was = ring.active || snoozePending;
  ring.active = false;
  snoozePending = false;
  if (pattern == BUZZ_ALARM) pattern = BUZZ_NONE;
  buzzerWrite(false);
  lastRender = 0;
  return was;
}

bool alarmSnooze() {
  if (!ring.active) return false;
  const uint8_t index = ring.index;
  const uint8_t minutes = index < alarms.size() ? alarms[index].snoozeMinutes : 9;
  alarmStop();
  snoozePending = true;
  snoozeIndex = index;
  snoozeAt = millis() + minutes * 60000UL;
  return true;
}

bool buttonStopsAlarm() {
  if (!ring.active && !snoozePending) return false;
  alarmStop();
  return true;
}

void buzzerTest() {
  if (!buzzerPinActive) setupBuzzer();
  startPattern(BUZZ_TEST, millis());
}

bool chimeFlashActive(uint32_t now) {
  return chimeUntil && static_cast<int32_t>(chimeUntil - now) > 0;
}

void serviceAlarms(uint32_t now) {
  // Buzzer pattern, evaluated every loop for accurate beeps.
  if (pattern != BUZZ_NONE) {
    bool finished;
    const bool on = patternLevel(pattern, now - patternStarted, finished);
    if (finished) pattern = BUZZ_NONE;
    buzzerWrite(on && !finished);
  }
  if (now - lastCheck < 250) return;
  lastCheck = now;

  if (ring.active && now - ring.startedMs >= ring.durationMs) alarmStop();
  if (snoozePending && static_cast<int32_t>(now - snoozeAt) >= 0) startRing(snoozeIndex, now);
  if (chimeUntil && static_cast<int32_t>(now - chimeUntil) >= 0) {
    chimeUntil = 0;
    lastRender = 0;
  }

  struct tm t;
  if (!timeIsReasonable() || !getLocalTime(&t, 0)) return;
  const int32_t key = static_cast<int32_t>(t.tm_yday) * 1440 + t.tm_hour * 60 + t.tm_min;
  if (key != lastTriggerKey) {
    lastTriggerKey = key;
    for (uint8_t i = 0; i < alarms.size(); i++) {
      const AlarmEntry &alarm = alarms[i];
      if (alarm.enabled && (alarm.days & (1 << t.tm_wday)) && alarm.hour == t.tm_hour && alarm.minute == t.tm_min) {
        startRing(i, now);
        break;
      }
    }
  }
  if (extras.hourlyChime && t.tm_min == 0 && t.tm_hour != lastChimeHour) {
    lastChimeHour = t.tm_hour;
    const bool window = extras.chimeFrom <= extras.chimeTo
      ? t.tm_hour >= extras.chimeFrom && t.tm_hour <= extras.chimeTo
      : t.tm_hour >= extras.chimeFrom || t.tm_hour <= extras.chimeTo;
    if (window && !ring.active) {
      chimeUntil = now + 900;
      lastRender = 0;
      if (extras.hourlyBeep) startPattern(BUZZ_CHIME, now);
    }
  } else if (t.tm_min != 0) {
    lastChimeHour = -1;
  }
}

void drawAlarm(uint32_t now) {
  const bool phase = (now / 300) % 2;
  if (ring.flash) {
    const CRGB bg = phase ? CRGB(90, 20, 0) : CRGB(10, 2, 0);
    for (int y = 0; y < config.height; y++)
      for (int x = 0; x < config.width; x++) px(x, y, bg);
  }
  const int wobble = ((now / 150) % 4 == 1) ? 1 : ((now / 150) % 4 == 3) ? -1 : 0;
  drawMessageIcon(ICON_BELL, 0 + wobble, (config.height - 8) / 2, 0);
  struct tm t;
  String text;
  if (getLocalTime(&t, 0)) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", t.tm_hour, t.tm_min);
    text = buffer;
  }
  const int left = MESSAGE_ICON_W + 1;
  const bool showLabel = !ring.label.isEmpty() && ((now - ring.startedMs) / 3000) % 2 == 1;
  if (showLabel) {
    const int width = textPixelWidth(ring.label.c_str(), textlayout::FONT_LARGE);
    const int area = config.width - left;
    int x = left + (area - width) / 2;
    if (width > area) x = left + area - static_cast<int>(((now - ring.startedMs) % 3000) * (area + width) / 3000);
    drawTextClipped(ring.label.c_str(), x, textTopY(textlayout::FONT_LARGE), textlayout::FONT_LARGE,
                    CRGB(255, 220, 120), left, config.width, false);
  } else {
    const int width = textPixelWidth(text.c_str(), textlayout::FONT_SMALL);
    drawTextClipped(text.c_str(), left + (config.width - left - width) / 2, textTopY(textlayout::FONT_SMALL),
                    textlayout::FONT_SMALL, CRGB(255, 220, 120), left, config.width, false);
  }
}
