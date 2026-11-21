#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <nvs_flash.h>

#include "app_state.h"
#include "features.h"

// Storage and web API for the PixelUhr features. All lists are stored as JSON
// blobs in the "userdata" NVS partition and only written on an explicit save.

std::vector<ScrollText> scrollTexts;
std::vector<CalendarEvent> calendarEvents;
std::vector<AlarmEntry> alarms;
ExtrasConfig extras;
std::vector<PageEntry> pageEntries;
std::vector<String> newsItems;
String newsError;
uint32_t newsFetchedAt = 0;
bool pendingNewsFetch = false;
uint32_t newsRevision = 0;

const char *const PAGE_KEYS[PAGE_TYPE_COUNT] = {
  "clock", "date", "weather", "weatherinfo", "texts", "events", "week", "sun",
  "moon", "quote", "news", "greeting", "wifi", "warning", "animation"
};

static const char *const HOLIDAY_REGIONS[] = {
  "BW", "BY", "BE", "BB", "HB", "HH", "HE", "MV", "NI", "NW", "RP", "SL", "SN", "ST", "SH", "TH"
};

int pageTypeFromKey(const String &key) {
  for (uint8_t i = 0; i < PAGE_TYPE_COUNT; i++) {
    if (key == PAGE_KEYS[i]) return i;
  }
  return -1;
}

// "clock:16,date:0,..." -> entries. Unknown keys and duplicates are dropped.
void parsePageOrder(const String &value, std::vector<PageEntry> &entries) {
  entries.clear();
  int start = 0;
  bool seen[PAGE_TYPE_COUNT] = {};
  while (start < static_cast<int>(value.length()) && entries.size() < 24) {
    int end = value.indexOf(',', start);
    if (end < 0) end = value.length();
    String item = value.substring(start, end);
    item.trim();
    start = end + 1;
    if (item.isEmpty()) continue;
    const int colon = item.indexOf(':');
    const String key = colon >= 0 ? item.substring(0, colon) : item;
    const int type = pageTypeFromKey(key);
    if (type < 0 || seen[type]) continue;
    seen[type] = true;
    const int seconds = colon >= 0 ? constrain(item.substring(colon + 1).toInt(), 0, 120) : 0;
    entries.push_back(PageEntry{static_cast<uint8_t>(type), static_cast<uint8_t>(seconds)});
  }
  if (entries.empty()) entries.push_back(PageEntry{PAGE_CLOCK, 0});
}

String normalizePageOrder(const String &value) {
  std::vector<PageEntry> entries;
  parsePageOrder(value, entries);
  String out;
  for (const PageEntry &entry : entries) {
    if (!out.isEmpty()) out += ',';
    out += PAGE_KEYS[entry.type];
    out += ':';
    out += String(entry.seconds);
  }
  return out;
}

// ---- Small format helpers --------------------------------------------------

static String formatDay(int32_t day) {
  if (day == cal::NO_DAY) return "";
  int y, m, d;
  cal::civilFromDays(day, y, m, d);
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", y, m, d);
  return buffer;
}

static bool parseDayParts(const char *text, int &y, int &m, int &d) {
  if (!text || strlen(text) < 10) return false;
  if (sscanf(text, "%d-%d-%d", &y, &m, &d) != 3) return false;
  return y >= 1900 && y <= 2200 && m >= 1 && m <= 12 && d >= 1 && d <= cal::daysInMonth(y, m);
}

static int32_t parseDay(const char *text) {
  int y, m, d;
  return parseDayParts(text, y, m, d) ? cal::daysFromCivil(y, m, d) : cal::NO_DAY;
}

static int16_t parseMinute(const char *text) {
  int h, m;
  if (!text || sscanf(text, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

static String formatMinute(int16_t minute) {
  if (minute < 0) return "";
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", minute / 60, minute % 60);
  return buffer;
}

static String limitText(const char *text, size_t max) {
  String value = text ? text : "";
  value.trim();
  if (value.length() > max) {
    size_t cut = max;
    // Do not split a UTF-8 sequence.
    while (cut > 0 && (static_cast<uint8_t>(value[cut]) & 0xC0) == 0x80) cut--;
    value = value.substring(0, cut);
  }
  return value;
}

static uint32_t jsonColor(JsonVariantConst value, uint32_t fallback) {
  if (value.is<const char *>()) return parseColor(value.as<const char *>(), fallback);
  return fallback;
}

// ---- JSON conversion ---------------------------------------------------------

static void textToJson(const ScrollText &t, JsonObject o) {
  o["enabled"] = t.enabled;
  o["text"] = t.text;
  o["color"] = colorToHex(t.color);
  o["background"] = colorToHex(t.background);
  o["speed"] = t.speed;
  o["direction"] = t.direction;
  o["font"] = t.font;
  o["repeats"] = t.repeats;
  o["pause"] = t.pauseSeconds;
  o["duration"] = t.durationSeconds;
  o["permanent"] = t.permanent;
  o["rainbow"] = t.rainbow;
  o["days"] = t.days;
  o["from"] = formatMinute(t.fromMinute);
  o["to"] = formatMinute(t.toMinute);
  o["date"] = formatDay(t.date);
  o["until"] = t.untilDay == cal::NO_DAY ? String("") : formatDay(t.untilDay) + "T" + formatMinute(t.untilMinute);
}

static bool textFromJson(JsonObjectConst o, ScrollText &t) {
  t.text = limitText(o["text"] | "", MAX_TEXT_LENGTH);
  if (t.text.isEmpty()) return false;
  t.enabled = o["enabled"] | true;
  t.color = jsonColor(o["color"], 0xFFF5BE);
  t.background = jsonColor(o["background"], 0);
  t.speed = constrain(o["speed"] | 5, 1, 10);
  t.direction = constrain(o["direction"] | 0, 0, 1);
  t.font = constrain(o["font"] | 0, 0, 1);
  t.repeats = constrain(o["repeats"] | 1, 1, 20);
  t.pauseSeconds = constrain(o["pause"] | 0, 0, 30);
  t.durationSeconds = constrain(o["duration"] | 0, 0, 3600);
  t.permanent = o["permanent"] | false;
  t.rainbow = o["rainbow"] | false;
  t.days = (o["days"] | 0x7F) & 0x7F;
  if (!t.days) t.days = 0x7F;
  t.fromMinute = parseMinute(o["from"] | "");
  t.toMinute = parseMinute(o["to"] | "");
  if (t.fromMinute < 0 || t.toMinute < 0) t.fromMinute = t.toMinute = -1;
  t.date = parseDay(o["date"] | "");
  const char *until = o["until"] | "";
  t.untilDay = parseDay(until);
  t.untilMinute = strlen(until) > 11 ? max<int16_t>(0, parseMinute(until + 11)) : 0;
  return true;
}

static void eventToJson(const CalendarEvent &e, JsonObject o) {
  o["enabled"] = e.enabled;
  o["show"] = e.show;
  o["name"] = e.name;
  o["description"] = e.description;
  o["text"] = e.text;
  char date[16];
  snprintf(date, sizeof(date), "%04d-%02d-%02d", e.year, e.month, e.day);
  o["date"] = date;
  o["time"] = formatMinute(e.minute);
  o["type"] = e.type;
  o["repeat"] = e.repeat;
  o["interval"] = e.interval;
  o["lead"] = e.leadDays;
  o["color"] = colorToHex(e.color);
}

static bool eventFromJson(JsonObjectConst o, CalendarEvent &e) {
  e.name = limitText(o["name"] | "", 60);
  int y, m, d;
  if (e.name.isEmpty() || !parseDayParts(o["date"] | "", y, m, d)) return false;
  e.year = y;
  e.month = m;
  e.day = d;
  e.enabled = o["enabled"] | true;
  e.show = o["show"] | true;
  e.description = limitText(o["description"] | "", 120);
  e.text = limitText(o["text"] | "", MAX_TEXT_LENGTH);
  e.minute = parseMinute(o["time"] | "");
  e.type = constrain(o["type"] | 0, 0, EVENT_TYPE_COUNT - 1);
  e.repeat = constrain(o["repeat"] | 0, 0, 3);
  e.interval = constrain(o["interval"] | 1, 1, 12);
  e.leadDays = constrain(o["lead"] | 7, 0, 366);
  e.color = jsonColor(o["color"], 0x50B4FF);
  return true;
}

static void alarmToJson(const AlarmEntry &a, JsonObject o) {
  o["enabled"] = a.enabled;
  o["time"] = formatMinute(a.hour * 60 + a.minute);
  o["days"] = a.days;
  o["label"] = a.label;
  o["buzzer"] = a.buzzer;
  o["flash"] = a.flash;
  o["duration"] = a.durationMinutes;
  o["snooze"] = a.snoozeMinutes;
}

static bool alarmFromJson(JsonObjectConst o, AlarmEntry &a) {
  const int16_t minute = parseMinute(o["time"] | "");
  if (minute < 0) return false;
  a.hour = minute / 60;
  a.minute = minute % 60;
  a.enabled = o["enabled"] | true;
  a.days = (o["days"] | 0x7F) & 0x7F;
  a.label = limitText(o["label"] | "", 40);
  a.buzzer = o["buzzer"] | true;
  a.flash = o["flash"] | true;
  a.durationMinutes = constrain(o["duration"] | 5, 1, 30);
  a.snoozeMinutes = constrain(o["snooze"] | 9, 1, 30);
  return true;
}

static bool validRegion(const String &region) {
  for (const char *code : HOLIDAY_REGIONS) {
    if (region == code) return true;
  }
  return false;
}

static void extrasToJson(const ExtrasConfig &x, JsonObject o) {
  o["holidays"] = x.holidays;
  o["holidayRegion"] = x.holidayRegion;
  o["observances"] = x.observances;
  o["holidayLeadDays"] = x.holidayLeadDays;
  o["greetingName"] = x.greetingName;
  o["quotes"] = x.quotes;
  o["quoteRandom"] = x.quoteRandom;
  o["newsUrl"] = x.newsUrl;
  o["newsCount"] = x.newsCount;
  o["newsIntervalMinutes"] = x.newsIntervalMinutes;
  o["warnFrost"] = x.warnFrost;
  o["frostLimit"] = x.frostLimit;
  o["warnHeat"] = x.warnHeat;
  o["heatLimit"] = x.heatLimit;
  o["warnStorm"] = x.warnStorm;
  o["stormLimit"] = x.stormLimit;
  o["warnThunder"] = x.warnThunder;
  o["detailDescription"] = x.detailDescription;
  o["detailFeels"] = x.detailFeels;
  o["detailMinMax"] = x.detailMinMax;
  o["detailRain"] = x.detailRain;
  o["detailWind"] = x.detailWind;
  o["detailHumidity"] = x.detailHumidity;
  o["hourlyChime"] = x.hourlyChime;
  o["hourlyBeep"] = x.hourlyBeep;
  o["chimeFrom"] = x.chimeFrom;
  o["chimeTo"] = x.chimeTo;
  o["animation"] = x.animation;
  o["birthdayConfetti"] = x.birthdayConfetti;
  o["messageSpeed"] = x.messageSpeed;
  o["messageColor"] = x.messageColor ? colorToHex(x.messageColor) : String("");
}

static void extrasFromJson(JsonObjectConst o, ExtrasConfig &x) {
  x.holidays = o["holidays"] | x.holidays;
  String region = o["holidayRegion"] | x.holidayRegion.c_str();
  region.toUpperCase();
  x.holidayRegion = validRegion(region) ? region : String("");
  x.observances = o["observances"] | x.observances;
  x.holidayLeadDays = constrain(o["holidayLeadDays"] | x.holidayLeadDays, 0, 30);
  x.greetingName = limitText(o["greetingName"] | x.greetingName.c_str(), 30);
  x.quotes = limitText(o["quotes"] | x.quotes.c_str(), MAX_QUOTES_LENGTH);
  x.quoteRandom = o["quoteRandom"] | x.quoteRandom;
  String url = limitText(o["newsUrl"] | x.newsUrl.c_str(), 240);
  if (!url.isEmpty() && !url.startsWith("http://") && !url.startsWith("https://")) url = "https://" + url;
  if (url != x.newsUrl) {
    ++newsRevision;
    newsItems.clear();
    newsError = "";
    newsFetchedAt = 0;
    pendingNewsFetch = !url.isEmpty();
  }
  x.newsUrl = url;
  x.newsCount = constrain(o["newsCount"] | x.newsCount, 1, MAX_NEWS);
  x.newsIntervalMinutes = constrain(o["newsIntervalMinutes"] | x.newsIntervalMinutes, 10, 240);
  x.warnFrost = o["warnFrost"] | x.warnFrost;
  x.frostLimit = constrain(o["frostLimit"] | x.frostLimit, -30, 10);
  x.warnHeat = o["warnHeat"] | x.warnHeat;
  x.heatLimit = constrain(o["heatLimit"] | x.heatLimit, 20, 45);
  x.warnStorm = o["warnStorm"] | x.warnStorm;
  x.stormLimit = constrain(o["stormLimit"] | x.stormLimit, 30, 150);
  x.warnThunder = o["warnThunder"] | x.warnThunder;
  x.detailDescription = o["detailDescription"] | x.detailDescription;
  x.detailFeels = o["detailFeels"] | x.detailFeels;
  x.detailMinMax = o["detailMinMax"] | x.detailMinMax;
  x.detailRain = o["detailRain"] | x.detailRain;
  x.detailWind = o["detailWind"] | x.detailWind;
  x.detailHumidity = o["detailHumidity"] | x.detailHumidity;
  x.hourlyChime = o["hourlyChime"] | x.hourlyChime;
  x.hourlyBeep = o["hourlyBeep"] | x.hourlyBeep;
  x.chimeFrom = constrain(o["chimeFrom"] | x.chimeFrom, 0, 23);
  x.chimeTo = constrain(o["chimeTo"] | x.chimeTo, 0, 23);
  x.animation = constrain(o["animation"] | x.animation, 0, 3);
  x.birthdayConfetti = o["birthdayConfetti"] | x.birthdayConfetti;
  x.messageSpeed = constrain(o["messageSpeed"] | x.messageSpeed, 1, 10);
  const char *color = o["messageColor"] | "";
  x.messageColor = strlen(color) ? parseColor(color, 0) : 0;
}

// ---- Storage -------------------------------------------------------------

static bool openUserData(Preferences &store, bool readOnly) {
  if (store.begin(USERDATA_NAMESPACE, readOnly, USERDATA_PARTITION)) return true;
  // A partition with foreign or outdated content must be reformatted once.
  if (!readOnly && nvs_flash_erase_partition(USERDATA_PARTITION) == ESP_OK) {
    return store.begin(USERDATA_NAMESPACE, readOnly, USERDATA_PARTITION);
  }
  return false;
}

static bool readBlob(Preferences &store, const char *key, JsonDocument &doc) {
  const size_t length = store.isKey(key) ? store.getBytesLength(key) : 0;
  if (!length) return false;
  std::unique_ptr<char[]> buffer(new (std::nothrow) char[length + 1]);
  if (!buffer) return false;
  store.getBytes(key, buffer.get(), length);
  buffer[length] = 0;
  return !deserializeJson(doc, buffer.get(), length);
}

static bool writeBlob(const char *key, const JsonDocument &doc) {
  String body;
  serializeJson(doc, body);
  Preferences store;
  if (!openUserData(store, false)) return false;
  bool ok = true;
  const size_t existing = store.isKey(key) ? store.getBytesLength(key) : 0;
  bool same = existing == body.length();
  if (same && existing) {
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[existing]);
    same = buffer && store.getBytes(key, buffer.get(), existing) == existing &&
           memcmp(buffer.get(), body.c_str(), existing) == 0;
  }
  if (!same) ok = store.putBytes(key, body.c_str(), body.length()) == body.length();
  store.end();
  return ok;
}

static void defaultTexts() {
  scrollTexts.clear();
  ScrollText welcome;
  welcome.text = "Herzlich willkommen!";
  welcome.enabled = false;
  scrollTexts.push_back(welcome);
}

static void defaultEvents() {
  calendarEvents.clear();
  CalendarEvent christmas;
  christmas.name = "Weihnachten";
  christmas.year = 2026;
  christmas.month = 12;
  christmas.day = 24;
  christmas.type = EVENT_COUNTDOWN;
  christmas.repeat = cal::REPEAT_YEARLY;
  christmas.leadDays = 30;
  christmas.color = 0xFF3030;
  calendarEvents.push_back(christmas);
}

void loadUserData() {
  Preferences store;
  const bool opened = openUserData(store, false);
  JsonDocument doc;
  bool hasTexts = false, hasEvents = false;
  if (opened && readBlob(store, "texts", doc)) {
    hasTexts = true;
    scrollTexts.clear();
    for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
      ScrollText t;
      if (textFromJson(o, t) && scrollTexts.size() < MAX_TEXTS) scrollTexts.push_back(t);
    }
  }
  doc.clear();
  if (opened && readBlob(store, "events", doc)) {
    hasEvents = true;
    calendarEvents.clear();
    for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
      CalendarEvent e;
      if (eventFromJson(o, e) && calendarEvents.size() < MAX_EVENTS) calendarEvents.push_back(e);
    }
  }
  doc.clear();
  if (opened && readBlob(store, "alarms", doc)) {
    alarms.clear();
    for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
      AlarmEntry a;
      if (alarmFromJson(o, a) && alarms.size() < MAX_ALARMS) alarms.push_back(a);
    }
  }
  doc.clear();
  if (opened && readBlob(store, "extras", doc)) extrasFromJson(doc.as<JsonObjectConst>(), extras);
  if (opened) store.end();
  if (!hasTexts) defaultTexts();
  if (!hasEvents) defaultEvents();
  pendingNewsFetch = !extras.newsUrl.isEmpty();
}

void eraseUserData() {
  nvs_flash_erase_partition(USERDATA_PARTITION);
}

// ---- Web API -------------------------------------------------------------

static void sendDoc(AsyncWebServerRequest *request, const JsonDocument &doc, int code = 200) {
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->setCode(code);
  response->addHeader("Cache-Control", "no-store");
  serializeJson(doc, *response);
  request->send(response);
}

static void sendTexts(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  StateLock lock;
  JsonDocument doc;
  JsonArray list = doc["texts"].to<JsonArray>();
  for (const ScrollText &t : scrollTexts) textToJson(t, list.add<JsonObject>());
  doc["max"] = MAX_TEXTS;
  doc["maxLength"] = MAX_TEXT_LENGTH;
  sendDoc(request, doc);
}

static void saveTexts(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!requireAdminAuth(request)) return;
  JsonArrayConst input = json["texts"].as<JsonArrayConst>();
  if (input.isNull()) return sendJsonError(request, 400, "Ungültige Lauftexte.");
  std::vector<ScrollText> next;
  JsonDocument stored;
  JsonArray out = stored.to<JsonArray>();
  for (JsonObjectConst o : input) {
    ScrollText t;
    if (!textFromJson(o, t)) continue;
    if (next.size() >= MAX_TEXTS) break;
    next.push_back(t);
    textToJson(t, out.add<JsonObject>());
  }
  if (!writeBlob("texts", stored)) return sendJsonError(request, 500, "Lauftexte konnten nicht gespeichert werden.");
  {
    StateLock lock;
    scrollTexts.swap(next);
    pagesReset();
  }
  sendTexts(request);
}

static void previewText(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!requireAdminAuth(request)) return;
  ScrollText text;
  if (!textFromJson(json["text"].as<JsonObjectConst>(), text)) return sendJsonError(request, 400, "Bitte einen Text eingeben.");
  StateLock lock;
  pagesPreviewText(text);
  request->send(200, "application/json", "{\"ok\":true}");
}

static void sendEvents(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  StateLock lock;
  JsonDocument doc;
  JsonArray list = doc["events"].to<JsonArray>();
  for (const CalendarEvent &e : calendarEvents) eventToJson(e, list.add<JsonObject>());
  doc["max"] = MAX_EVENTS;
  describeUpcoming(doc["upcoming"].to<JsonArray>(), 12);
  describeHolidays(doc["holidays"].to<JsonArray>());
  sendDoc(request, doc);
}

static void saveEvents(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!requireAdminAuth(request)) return;
  JsonArrayConst input = json["events"].as<JsonArrayConst>();
  if (input.isNull()) return sendJsonError(request, 400, "Ungültige Termine.");
  std::vector<CalendarEvent> next;
  JsonDocument stored;
  JsonArray out = stored.to<JsonArray>();
  for (JsonObjectConst o : input) {
    CalendarEvent e;
    if (!eventFromJson(o, e)) continue;
    if (next.size() >= MAX_EVENTS) break;
    next.push_back(e);
    eventToJson(e, out.add<JsonObject>());
  }
  if (!writeBlob("events", stored)) return sendJsonError(request, 500, "Termine konnten nicht gespeichert werden.");
  {
    StateLock lock;
    calendarEvents.swap(next);
    pagesReset();
  }
  sendEvents(request);
}

static void sendAlarms(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  StateLock lock;
  JsonDocument doc;
  JsonArray list = doc["alarms"].to<JsonArray>();
  for (const AlarmEntry &a : alarms) alarmToJson(a, list.add<JsonObject>());
  doc["max"] = MAX_ALARMS;
  doc["ringing"] = alarmRinging();
  doc["buzzerPin"] = config.buzzerPin;
  sendDoc(request, doc);
}

static void saveAlarms(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!requireAdminAuth(request)) return;
  JsonArrayConst input = json["alarms"].as<JsonArrayConst>();
  if (input.isNull()) return sendJsonError(request, 400, "Ungültige Weckzeiten.");
  std::vector<AlarmEntry> next;
  JsonDocument stored;
  JsonArray out = stored.to<JsonArray>();
  for (JsonObjectConst o : input) {
    AlarmEntry a;
    if (!alarmFromJson(o, a)) continue;
    if (next.size() >= MAX_ALARMS) break;
    next.push_back(a);
    alarmToJson(a, out.add<JsonObject>());
  }
  if (!writeBlob("alarms", stored)) return sendJsonError(request, 500, "Wecker konnten nicht gespeichert werden.");
  {
    StateLock lock;
    alarms.swap(next);
  }
  sendAlarms(request);
}

static void sendExtras(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  StateLock lock;
  JsonDocument doc;
  extrasToJson(extras, doc["extras"].to<JsonObject>());
  JsonObject news = doc["news"].to<JsonObject>();
  JsonArray items = news["items"].to<JsonArray>();
  for (const String &item : newsItems) items.add(item);
  news["error"] = newsError;
  if (newsFetchedAt) news["ageMs"] = millis() - newsFetchedAt;
  sendDoc(request, doc);
}

static void saveExtras(AsyncWebServerRequest *request, JsonVariant &json) {
  if (!requireAdminAuth(request)) return;
  JsonObjectConst input = json["extras"].as<JsonObjectConst>();
  if (input.isNull()) return sendJsonError(request, 400, "Ungültige Einstellungen.");
  JsonDocument stored;
  {
    StateLock lock;
    extrasFromJson(input, extras);
    extrasToJson(extras, stored.to<JsonObject>());
    pagesReset();
  }
  if (!writeBlob("extras", stored)) return sendJsonError(request, 500, "Einstellungen konnten nicht gespeichert werden.");
  sendExtras(request);
}

static AsyncCallbackJsonWebHandler *jsonRoute(const char *uri, ArJsonRequestHandlerFunction fn, size_t maxLength) {
  AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler(uri, fn);
  handler->setMethod(HTTP_POST);
  handler->setMaxContentLength(maxLength);
  return handler;
}

void registerFeatureRoutes(AsyncWebServer &web) {
  // JSON POST handlers first: GET routes with the same path would also match.
  web.addHandler(jsonRoute("/api/texts/preview", previewText, 2000));
  web.addHandler(jsonRoute("/api/texts", saveTexts, 12000));
  web.addHandler(jsonRoute("/api/events", saveEvents, 24000));
  web.addHandler(jsonRoute("/api/alarms", saveAlarms, 4000));
  web.addHandler(jsonRoute("/api/extras", saveExtras, 8000));
  web.on("/api/texts", HTTP_GET, sendTexts);
  web.on("/api/events", HTTP_GET, sendEvents);
  web.on("/api/alarms", HTTP_GET, sendAlarms);
  web.on("/api/extras", HTTP_GET, sendExtras);
  web.on("/api/alarm/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAdminAuth(request)) return;
    StateLock lock;
    request->send(200, "application/json", alarmStop() ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  web.on("/api/alarm/snooze", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAdminAuth(request)) return;
    StateLock lock;
    request->send(200, "application/json", alarmSnooze() ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  web.on("/api/buzzer/test", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAdminAuth(request)) return;
    StateLock lock;
    if (!config.buzzerPin) return sendJsonError(request, 400, "Kein Summer-Pin eingestellt.");
    buzzerTest();
    request->send(200, "application/json", "{\"ok\":true}");
  });
  web.on("/api/news/refresh", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAdminAuth(request)) return;
    StateLock lock;
    if (extras.newsUrl.isEmpty()) return sendJsonError(request, 400, "Keine Nachrichten-Adresse eingestellt.");
    pendingNewsFetch = true;
    request->send(200, "application/json", "{\"ok\":true}");
  });
}

void addFeatureStatus(JsonObject status) {
  status["alarmRinging"] = alarmRinging();
  status["page"] = PAGE_KEYS[pagesCurrentType()];
  status["newsCount"] = newsItems.size();
  if (!newsError.isEmpty()) status["newsError"] = newsError;
  int16_t rise, set;
  if (sunTimesToday(rise, set)) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", rise / 60, rise % 60);
    status["sunrise"] = buffer;
    snprintf(buffer, sizeof(buffer), "%02d:%02d", set / 60, set % 60);
    status["sunset"] = buffer;
  }
  const double age = cal::moonAge(static_cast<double>(time(nullptr)));
  const uint8_t phase = cal::moonPhaseIndex(age);
  status["moonPhase"] = cal::moonPhaseName(phase, config.language == "en");
  status["moonIllumination"] = cal::moonIllumination(age);
  struct tm t;
  if (getLocalTime(&t, 10)) status["calendarWeek"] = cal::isoWeek(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}
