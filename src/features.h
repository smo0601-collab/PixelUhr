#pragma once
// PixelUhr features: page sequence, scrolling texts, calendar events,
// holidays, alarms with buzzer, news, quotes and the other info pages.
//
// User data (texts, events, alarms, extras) lives as JSON in the separate NVS
// partition "userdata". It survives firmware and web interface updates; only a
// factory reset erases it.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <vector>

#include "calendar_logic.h"

constexpr uint8_t MAX_TEXTS = 12;
constexpr uint8_t MAX_EVENTS = 40;
constexpr uint8_t MAX_ALARMS = 6;
constexpr uint8_t MAX_NEWS = 10;
constexpr size_t MAX_TEXT_LENGTH = 200;
constexpr size_t MAX_QUOTES_LENGTH = 3000;
constexpr const char *USERDATA_PARTITION = "userdata";
constexpr const char *USERDATA_NAMESPACE = "pixeluhr";

// ---- Page sequence -------------------------------------------------------
enum PageType : uint8_t {
  PAGE_CLOCK = 0,
  PAGE_DATE,
  PAGE_WEATHER,
  PAGE_WEATHERINFO,
  PAGE_TEXTS,
  PAGE_EVENTS,
  PAGE_WEEK,
  PAGE_SUN,
  PAGE_MOON,
  PAGE_QUOTE,
  PAGE_NEWS,
  PAGE_GREETING,
  PAGE_WIFI,
  PAGE_WARNING,
  PAGE_ANIMATION,
  PAGE_TYPE_COUNT
};

extern const char *const PAGE_KEYS[PAGE_TYPE_COUNT];
constexpr const char *DEFAULT_PAGE_ORDER = "clock:0,date:0,weather:0,warning:0,events:0,texts:0";

struct PageEntry {
  uint8_t type;
  uint8_t seconds;  // 0 = default duration
};

String normalizePageOrder(const String &value);
void parsePageOrder(const String &value, std::vector<PageEntry> &entries);
int pageTypeFromKey(const String &key);

// ---- Scrolling texts -----------------------------------------------------
struct ScrollText {
  bool enabled = true;
  String text;
  uint32_t color = 0xFFF5BE;
  uint32_t background = 0;
  uint8_t speed = 5;             // 1 (slow) ... 10 (fast)
  uint8_t direction = 0;         // 0 right to left, 1 left to right
  uint8_t font = 0;              // 0 large 5x7, 1 small 3x5
  uint8_t repeats = 1;           // 1 ... 20
  uint8_t pauseSeconds = 0;      // pause between repetitions
  uint16_t durationSeconds = 0;  // 0 = until all repetitions are done
  bool permanent = false;        // takes over the display while active
  bool rainbow = false;
  uint8_t days = 0x7F;           // bit 0 = Sunday ... bit 6 = Saturday
  int16_t fromMinute = -1;       // daily time window, -1 = all day
  int16_t toMinute = -1;
  int32_t date = cal::NO_DAY;    // only on this day
  int32_t untilDay = cal::NO_DAY;  // automatically ends at untilDay/untilMinute
  int16_t untilMinute = 0;
};

// ---- Calendar ------------------------------------------------------------
enum EventType : uint8_t {
  EVENT_APPOINTMENT = 0,
  EVENT_BIRTHDAY,
  EVENT_WASTE,
  EVENT_COUNTDOWN,
  EVENT_VACATION,
  EVENT_REMINDER,
  EVENT_TYPE_COUNT
};

struct CalendarEvent {
  bool enabled = true;
  bool show = true;            // false: only kept in the calendar
  String name;
  String description;
  String text;                 // optional custom message with placeholders
  int16_t year = 2026;
  uint8_t month = 1;
  uint8_t day = 1;
  int16_t minute = -1;         // time of day, -1 = all day
  uint8_t type = EVENT_APPOINTMENT;
  uint8_t repeat = cal::REPEAT_NONE;
  uint8_t interval = 1;
  uint16_t leadDays = 7;       // shown from this many days before
  uint32_t color = 0x50B4FF;
};

// ---- Alarms --------------------------------------------------------------
struct AlarmEntry {
  bool enabled = true;
  uint8_t hour = 7;
  uint8_t minute = 0;
  uint8_t days = 0x3E;         // Monday to Friday
  String label;
  bool buzzer = true;
  bool flash = true;
  uint8_t durationMinutes = 5;
  uint8_t snoozeMinutes = 9;
};

// ---- Extras --------------------------------------------------------------
struct ExtrasConfig {
  bool holidays = true;
  String holidayRegion;        // Bundesland code, empty = nationwide only
  bool observances = true;     // Heiligabend, Silvester, Advent, ...
  uint8_t holidayLeadDays = 1;
  String greetingName;
  String quotes;               // one per line; empty = built-in list
  bool quoteRandom = false;
  String newsUrl;
  uint8_t newsCount = 5;
  uint8_t newsIntervalMinutes = 30;
  bool warnFrost = true;
  int8_t frostLimit = 0;
  bool warnHeat = true;
  int8_t heatLimit = 30;
  bool warnStorm = true;
  uint8_t stormLimit = 60;     // km/h gusts
  bool warnThunder = true;
  bool detailDescription = true;
  bool detailFeels = true;
  bool detailMinMax = true;
  bool detailRain = true;
  bool detailWind = true;
  bool detailHumidity = true;
  bool hourlyChime = false;
  bool hourlyBeep = false;
  uint8_t chimeFrom = 7;
  uint8_t chimeTo = 22;
  uint8_t animation = 0;       // 0 rainbow, 1 sparkle, 2 snow, 3 matrix
  bool birthdayConfetti = true;
  uint8_t messageSpeed = 5;    // speed for events, news, weather details ...
  uint32_t messageColor = 0;   // 0 = use the text colour of the display page
};

extern std::vector<ScrollText> scrollTexts;
extern std::vector<CalendarEvent> calendarEvents;
extern std::vector<AlarmEntry> alarms;
extern ExtrasConfig extras;
extern std::vector<PageEntry> pageEntries;
extern std::vector<String> newsItems;
extern String newsError;
extern uint32_t newsFetchedAt;
extern bool pendingNewsFetch;
extern uint32_t newsRevision;

// Storage and web API (features.cpp)
void loadUserData();
void eraseUserData();
void registerFeatureRoutes(AsyncWebServer &server);
void addFeatureStatus(JsonObject status);

// Pages and messages (pages.cpp)
void pagesReset();
void pagesPreviewText(const ScrollText &text);  // shows a text once, right now
bool pagesDraw(uint32_t now);      // returns true while an animation runs
bool pagesAnimating();
uint8_t pagesCurrentType();
String expandPlaceholders(const String &text);
int32_t localToday(int *wday = nullptr, int *minuteOfDay = nullptr);
void describeUpcoming(JsonArray out, size_t max);
void describeHolidays(JsonArray out);

// Alarms, hourly chime and buzzer (alarm.cpp)
void setupBuzzer();
void serviceAlarms(uint32_t now);
bool alarmRinging();
void drawAlarm(uint32_t now);
bool alarmStop();
bool alarmSnooze();
void buzzerTest();
bool chimeFlashActive(uint32_t now);
bool buttonStopsAlarm();

// Sun times for night mode and the sun page (pages.cpp)
bool sunTimesToday(int16_t &riseMinute, int16_t &setMinute);

// News (news.cpp), runs in the network worker
bool newsFetchDue(uint32_t now);
void fetchNews();
