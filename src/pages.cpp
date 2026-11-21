#include <WiFi.h>
#include <algorithm>

#include "app_state.h"
#include "display_internal.h"
#include "features.h"
#include "text_layout.h"
#include "weather_icons.h"

// Page sequence and the message player. Info pages (texts, calendar, news,
// weather details, ...) are built as a playlist of messages; each message is
// shown centered when it fits or scrolls through otherwise.

namespace {

struct Message {
  String text;
  uint32_t color = 0;          // 0 = text colour of the display settings
  uint32_t background = 0;
  uint8_t font = textlayout::FONT_LARGE;
  uint8_t speed = 5;
  uint8_t repeats = 1;
  uint8_t icon = ICON_NONE;
  uint32_t iconColor = 0;
  bool reverse = false;
  bool rainbow = false;
  bool confetti = false;
  uint16_t pauseMs = 0;
  uint32_t maxMs = 0;          // hard limit, 0 = none
  uint32_t staticMs = 0;       // display time of a fitting text, 0 = page default
};

class Player {
 public:
  void load(std::vector<Message> &&messages, uint32_t now) {
    list_ = std::move(messages);
    index_ = 0;
    start(now);
  }
  void clear() { list_.clear(); index_ = 0; }
  bool empty() const { return list_.empty(); }
  bool done() const { return index_ >= list_.size(); }
  bool animating() const {
    if (done()) return false;
    const Message &m = list_[index_];
    return scrolling_ || m.rainbow || m.confetti || config.colorGradientMode != 0;
  }

  // Draws the current message; advances to the next one when it is finished.
  void draw(uint32_t now) {
    while (!done() && finished(now)) {
      ++index_;
      if (!done()) start(now);
    }
    if (done()) return;
    const Message &m = list_[index_];
    const int left = m.icon ? MESSAGE_ICON_W + 1 : 0;
    const int right = config.width;
    if (m.background) {
      const CRGB bg = packedColor(m.background);
      for (int y = 0; y < config.height; y++)
        for (int x = left; x < right; x++) px(x, y, bg);
    }
    if (m.confetti) drawConfetti(now);
    if (m.icon) drawMessageIcon(m.icon, 0, (config.height - 8) / 2, m.iconColor);
    const CRGB color = packedColor(m.color ? m.color : config.colorText);
    const int y = textTopY(m.font);
    const uint32_t elapsed = now - started_;
    if (!scrolling_) {
      drawTextClipped(m.text.c_str(), left + (right - left - width_) / 2, y, m.font, color, left, right, m.rainbow);
      return;
    }
    const uint32_t slot = cycleMs_ + m.pauseMs;
    const uint32_t inCycle = elapsed % slot;
    if (inCycle >= cycleMs_) return;  // pause between repetitions
    const int offset = static_cast<int>(static_cast<uint64_t>(inCycle) * pixelsPerSecond_ / 1000);
    const int x = m.reverse ? left - width_ + offset : right - offset;
    drawTextClipped(m.text.c_str(), x, y, m.font, color, left, right, m.rainbow);
  }

 private:
  void start(uint32_t now) {
    started_ = now;
    const Message &m = list_[index_];
    const int left = m.icon ? MESSAGE_ICON_W + 1 : 0;
    const int area = config.width - left;
    width_ = textPixelWidth(m.text.c_str(), m.font);
    scrolling_ = width_ > area;
    pixelsPerSecond_ = 6 + constrain(m.speed, 1, 10) * 4;
    cycleMs_ = static_cast<uint32_t>(area + width_) * 1000UL / pixelsPerSecond_;
  }

  bool finished(uint32_t now) const {
    const Message &m = list_[index_];
    const uint32_t elapsed = now - started_;
    if (m.maxMs && elapsed >= m.maxMs) return true;
    if (!scrolling_) {
      const uint32_t staticMs = m.staticMs ? m.staticMs : config.pageSeconds * 1000UL;
      return elapsed >= staticMs;
    }
    const uint32_t total = m.repeats * (cycleMs_ + m.pauseMs) - m.pauseMs;
    return elapsed >= total;
  }

  static void drawConfetti(uint32_t now) {
    const uint32_t frame = now / 90;
    for (uint8_t i = 0; i < 6; i++) {
      uint32_t h = (frame + i * 977) * 2654435761UL;
      const int x = (h >> 8) % config.width;
      const int y = (h >> 16) % config.height;
      px(x, y, CHSV((h >> 24) & 0xff, 200, 90));
    }
  }

  std::vector<Message> list_;
  size_t index_ = 0;
  uint32_t started_ = 0;
  int width_ = 0;
  bool scrolling_ = false;
  int pixelsPerSecond_ = 26;
  uint32_t cycleMs_ = 1000;
};

Player pagePlayer;
Player permanentPlayer;
Player previewPlayer;
uint32_t previewUntil = 0;
int activeType = -1;
size_t entryIndex = 0;
uint32_t entryStarted = 0;
uint32_t permanentSignature = 0;
bool permanentActive = false;

bool english() { return config.language == "en"; }

}  // namespace

// ---- Icons ---------------------------------------------------------------

struct IconArt {
  const char *rows[8];
  uint32_t main;
  uint32_t accent;
};

static const IconArt ICON_ART[] = {
  /* NONE */ {{"", "", "", "", "", "", "", ""}, 0, 0},
  /* CALENDAR */ {{".+.....+.", "+++++++++", "#.......#", "#.#.#.#.#", "#.......#", "#.#.#.#.#", "#.......#", "#########"}, 0xC8C8C8, 0xFF3020},
  /* CAKE */ {{"..+.+.+..", "..#.#.#..", ".#######.", ".#.#.#.#.", ".#######.", "#########", "#########", "........."}, 0xFF50A0, 0xFFD000},
  /* TRASH */ {{"...+++...", "+++++++++", ".#######.", ".#.#.#.#.", ".#.#.#.#.", ".#.#.#.#.", ".#.#.#.#.", "..#####.."}, 0x30C040, 0x909090},
  /* HOURGLASS */ {{"#########", ".#.....#.", "..#+++#..", "...#+#...", "...#.#...", "..#.+.#..", ".#+++++#.", "#########"}, 0xC8A050, 0xFFD000},
  /* STAR */ {{"....#....", "...###...", "#########", ".#######.", "..#####..", "..##.##..", ".##...##.", "#.......#"}, 0xFFC000, 0},
  /* SUITCASE */ {{"...###...", "...#.#...", "#########", "#+#####+#", "#+#####+#", "#+#####+#", "#########", "........."}, 0x2090FF, 0xFFFFFF},
  /* WARNING */ {{"....#....", "...#+#...", "...#+#...", "..##+##..", "..##+##..", ".#######.", ".###+###.", "#########"}, 0xFF8000, 0},
  /* BELL */ {{"....+....", "...###...", "..#####..", "..#####..", "..#####..", ".#######.", "#########", "....+...."}, 0xFFC000, 0xFFF0A0},
  /* NEWS */ {{"########.", "#......##", "#.++.#.##", "#.++.#.##", "#......##", "#.####.##", "#......#.", "########."}, 0xD0D0D0, 0x3090FF},
  /* SUNRISE */ {{"....+....", "...+++...", "..+.+.+..", "..#####..", ".#######.", "#########", ".........", "#########"}, 0xFF7000, 0xFFD000},
  /* SUNSET */ {{"..+.+.+..", "...+++...", "....+....", "..#####..", ".#######.", "#########", ".........", "#########"}, 0xFF4000, 0xFFA000},
  /* MOON */ {{"", "", "", "", "", "", "", ""}, 0, 0},
  /* WIFI */ {{"", "", "", "", "", "", "", ""}, 0, 0},
  /* WEATHER */ {{"", "", "", "", "", "", "", ""}, 0, 0},
  /* HEART */ {{".##...##.", "####.####", "#########", "#########", ".#######.", "..#####..", "...###...", "....#...."}, 0xFF2040, 0},
};

static void drawMoon(int x, int y) {
  const double age = cal::moonAge(static_cast<double>(time(nullptr)));
  const double k = cos(2.0 * M_PI * age / cal::SYNODIC_MONTH);
  const bool waxing = age < cal::SYNODIC_MONTH / 2.0;
  const CRGB lit(255, 236, 160);
  const CRGB dark(22, 22, 34);
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      const double u = (col - 3.5) / 3.8, v = (row - 3.5) / 3.8;
      if (u * u + v * v > 1.0) continue;
      const double half = sqrt(1.0 - v * v);
      const double terminator = k * half;
      const bool on = waxing ? u > terminator : u < -terminator;
      px(x + col, y + row, on ? lit : dark);
    }
  }
}

static void drawWifiBars(int x, int y) {
  const int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -100;
  const int quality = constrain(2 * (rssi + 100), 0, 100);
  const uint8_t level = quality >= 75 ? 3 : quality >= 50 ? 2 : quality >= 20 ? 1 : 0;
  const uint8_t heights[] = {3, 5, 8};
  for (uint8_t bar = 0; bar < 3; bar++) {
    const CRGB color = bar < level ? CRGB(60, 200, 90) : CRGB(30, 30, 30);
    for (uint8_t h = 0; h < heights[bar]; h++) {
      px(x + bar * 3, y + 7 - h, color);
      px(x + bar * 3 + 1, y + 7 - h, color);
    }
  }
}

void drawMessageIcon(uint8_t icon, int x, int y, uint32_t color) {
  if (icon == ICON_MOON) return drawMoon(x, y);
  if (icon == ICON_WIFI) return drawWifiBars(x, y);
  if (icon == ICON_WEATHER) return drawWeatherIcon(x, y);
  if (icon >= sizeof(ICON_ART) / sizeof(ICON_ART[0])) return;
  const IconArt &art = ICON_ART[icon];
  const CRGB main = packedColor(color ? color : art.main);
  const CRGB accent = packedColor(art.accent);
  for (int row = 0; row < 8; row++) {
    const char *line = art.rows[row];
    for (int col = 0; col < 9 && line[col]; col++) {
      if (line[col] == '#') px(x + col, y + row, main);
      else if (line[col] == '+' && art.accent) px(x + col, y + row, accent);
    }
  }
}

// ---- Text drawing ----------------------------------------------------------

int textPixelWidth(const char *text, uint8_t font) {
  return textlayout::textWidth(text, font);
}

int textTopY(uint8_t font) {
  return font == textlayout::FONT_SMALL ? (config.height - 5) / 2 : (config.height - 8) / 2;
}

int drawTextClipped(const char *text, int x, int y, uint8_t font, CRGB color, int clipLeft, int clipRight,
                    bool rainbow) {
  const uint8_t hueBase = static_cast<uint8_t>(millis() / 12);
  return textlayout::drawText(text, x, y, font, [&](int px_, int py_) {
    if (px_ < clipLeft || px_ >= clipRight) return;
    if (rainbow) px(px_, py_, CHSV(hueBase + px_ * 8, 230, 255));
    else px(px_, py_, textPixelColor(px_, py_, color));
  });
}

// ---- Date helpers ----------------------------------------------------------

int32_t localToday(int *wday, int *minuteOfDay) {
  struct tm t;
  if (!getLocalTime(&t, 10)) return cal::NO_DAY;
  if (wday) *wday = t.tm_wday;
  if (minuteOfDay) *minuteOfDay = t.tm_hour * 60 + t.tm_min;
  return cal::daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

static String formatDate(int32_t day) {
  int y, m, d;
  cal::civilFromDays(day, y, m, d);
  char buffer[24];
  snprintf(buffer, sizeof(buffer), english() ? "%02d/%02d/%04d" : "%02d.%02d.%04d",
           english() ? m : d, english() ? d : m, y);
  return buffer;
}

static String formatClock(int minute) {
  char buffer[16];
  if (config.hourFormat == 12) {
    int h = (minute / 60) % 12;
    if (!h) h = 12;
    snprintf(buffer, sizeof(buffer), "%d:%02d%s", h, minute % 60, minute >= 720 ? "pm" : "am");
  } else {
    snprintf(buffer, sizeof(buffer), "%02d:%02d", minute / 60, minute % 60);
  }
  return buffer;
}

static String temperatureText(float celsius) {
  if (isnan(celsius)) return "--";
  return String(static_cast<int>(round(displayTemperature(celsius)))) + "\xC2\xB0" + temperatureUnitText();
}

bool sunTimesToday(int16_t &riseMinute, int16_t &setMinute) {
  static int32_t cachedDay = cal::NO_DAY;
  static float cachedLat = NAN, cachedLon = NAN;
  static int16_t rise = -1, set = -1;
  const int32_t today = localToday();
  if (today == cal::NO_DAY) return false;
  if (today != cachedDay || cachedLat != config.latitude || cachedLon != config.longitude) {
    cachedDay = today;
    cachedLat = config.latitude;
    cachedLon = config.longitude;
    rise = set = -1;
    int y, m, d;
    cal::civilFromDays(today, y, m, d);
    double hours;
    for (int i = 0; i < 2; i++) {
      if (!cal::sunEvent(y, m, d, config.latitude, config.longitude, i == 0, hours)) continue;
      const time_t epoch = static_cast<time_t>(today) * 86400 + static_cast<time_t>(hours * 3600.0);
      struct tm local;
      localtime_r(&epoch, &local);
      (i == 0 ? rise : set) = local.tm_hour * 60 + local.tm_min;
    }
  }
  if (rise < 0 || set < 0) return false;
  riseMinute = rise;
  setMinute = set;
  return true;
}

String expandPlaceholders(const String &input) {
  if (input.indexOf('{') < 0) return input;
  struct tm t;
  const bool haveTime = getLocalTime(&t, 10);
  const int32_t today = haveTime ? cal::daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) : cal::NO_DAY;
  static const char *weekdaysDe[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
  static const char *weekdaysEn[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  String out;
  out.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); i++) {
    if (input[i] != '{') { out += input[i]; continue; }
    const int close = input.indexOf('}', i);
    if (close < 0) { out += input.substring(i); break; }
    String key = input.substring(i + 1, close);
    key.toLowerCase();
    String value;
    bool known = true;
    if (key == "zeit" || key == "time") value = haveTime ? formatClock(t.tm_hour * 60 + t.tm_min) : "--:--";
    else if (key == "datum" || key == "date") value = today != cal::NO_DAY ? formatDate(today) : "";
    else if (key == "wochentag" || key == "weekday") value = haveTime ? (english() ? weekdaysEn : weekdaysDe)[t.tm_wday] : "";
    else if (key == "temp") value = temperatureText(weather.temperature);
    else if (key == "kw" || key == "week") value = haveTime ? String(cal::isoWeek(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday)) : "";
    else if (key == "name") value = extras.greetingName;
    else if (key.startsWith("tage:") || key.startsWith("days:")) {
      int y, m, d;
      if (sscanf(key.c_str() + 5, "%d-%d-%d", &y, &m, &d) == 3 && today != cal::NO_DAY) {
        value = String(max<int32_t>(0, cal::daysFromCivil(y, m, d) - today));
      }
    } else known = false;
    if (known) out += value;
    else out += input.substring(i, close + 1);
    i = close;
  }
  return out;
}

// ---- Schedules -------------------------------------------------------------

static bool textScheduledNow(const ScrollText &text, int32_t today, int wday, int minute) {
  if (!text.enabled || today == cal::NO_DAY) return false;
  if (!(text.days & (1 << wday))) return false;
  if (text.date != cal::NO_DAY && text.date != today) return false;
  if (text.untilDay != cal::NO_DAY &&
      (today > text.untilDay || (today == text.untilDay && minute >= text.untilMinute))) return false;
  if (text.fromMinute >= 0 && text.toMinute >= 0 && text.fromMinute != text.toMinute) {
    const bool inside = text.fromMinute < text.toMinute
      ? minute >= text.fromMinute && minute < text.toMinute
      : minute >= text.fromMinute || minute < text.toMinute;
    if (!inside) return false;
  }
  return true;
}

static Message messageFromText(const ScrollText &text) {
  Message m;
  m.text = expandPlaceholders(text.text);
  m.color = text.color;
  m.background = text.background;
  m.font = text.font;
  m.speed = text.speed;
  m.reverse = text.direction == 1;
  m.repeats = text.repeats;
  m.pauseMs = text.pauseSeconds * 1000;
  m.maxMs = text.durationSeconds * 1000UL;
  m.rainbow = text.rainbow;
  m.staticMs = text.durationSeconds ? text.durationSeconds * 1000UL : config.pageSeconds * 1000UL * text.repeats;
  return m;
}

static Message infoMessage(const String &text, uint8_t icon = ICON_NONE, uint32_t iconColor = 0) {
  Message m;
  m.text = text;
  m.icon = icon;
  m.iconColor = iconColor;
  m.speed = extras.messageSpeed;
  m.color = extras.messageColor;
  return m;
}

// ---- Calendar messages -----------------------------------------------------

struct Upcoming {
  int32_t day;
  int32_t daysUntil;
  String text;
  uint8_t icon;
  uint32_t color;
  bool confetti;
  int16_t minute;
};

static String replaceAll(String text, const char *key, const String &value) {
  text.replace(key, value);
  return text;
}

static String eventMessage(const CalendarEvent &e, int32_t occurrence, int32_t daysUntil) {
  const bool en = english();
  int oy, om, od;
  cal::civilFromDays(occurrence, oy, om, od);
  const int age = (e.type == EVENT_BIRTHDAY && e.repeat == cal::REPEAT_YEARLY && e.year > 1900 && oy > e.year) ? oy - e.year : 0;
  const String time = e.minute >= 0 ? formatClock(e.minute) : "";
  if (!e.text.isEmpty()) {
    String text = e.text;
    for (const char *key : {"{name}"}) text = replaceAll(text, key, e.name);
    text = replaceAll(replaceAll(text, "{tage}", String(daysUntil)), "{days}", String(daysUntil));
    text = replaceAll(replaceAll(text, "{datum}", formatDate(occurrence)), "{date}", formatDate(occurrence));
    text = replaceAll(replaceAll(text, "{zeit}", time), "{time}", time);
    text = replaceAll(replaceAll(text, "{alter}", String(age)), "{age}", String(age));
    return expandPlaceholders(text);
  }
  const String at = time.isEmpty() ? String("") : (en ? " at " : " um ") + time;
  String text;
  switch (e.type) {
    case EVENT_BIRTHDAY:
      if (daysUntil == 0) text = en ? "Happy birthday, " + e.name + "!" + (age ? " (" + String(age) + ")" : "")
                                    : "Heute hat " + e.name + " Geburtstag!" + (age ? " (" + String(age) + ")" : "");
      else if (daysUntil == 1) text = en ? e.name + "'s birthday is tomorrow" + (age ? " (" + String(age) + ")" : "")
                                         : "Morgen hat " + e.name + " Geburtstag" + (age ? " und wird " + String(age) : "");
      else text = en ? String(daysUntil) + " days until " + e.name + "'s birthday"
                     : "Noch " + String(daysUntil) + " Tage bis zum Geburtstag von " + e.name;
      break;
    case EVENT_WASTE:
      if (daysUntil == 0) text = (en ? "Collection today: " : "Heute Abholung: ") + e.name + at;
      else if (daysUntil == 1) text = (en ? "Tomorrow: put out the " : "Morgen Abholung: ") + e.name;
      else text = en ? e.name + " in " + String(daysUntil) + " days" : "In " + String(daysUntil) + " Tagen: " + e.name;
      break;
    case EVENT_COUNTDOWN:
      if (daysUntil == 0) text = en ? "Today is " + e.name + "!" : "Heute ist " + e.name + "!";
      else if (daysUntil == 1) text = en ? "Tomorrow is " + e.name + "!" : "Morgen ist " + e.name + "!";
      else text = en ? String(daysUntil) + " days until " + e.name : "Noch " + String(daysUntil) + " Tage bis " + e.name;
      break;
    case EVENT_VACATION:
      if (daysUntil == 0) text = en ? "Today: " + e.name + " - have fun!" : "Heute geht's los: " + e.name + "!";
      else if (daysUntil == 1) text = en ? "Tomorrow: " + e.name + "!" : "Morgen geht's los: " + e.name + "!";
      else text = en ? String(daysUntil) + " days until " + e.name : "Noch " + String(daysUntil) + " Tage bis " + e.name;
      break;
    case EVENT_REMINDER:
      if (daysUntil == 0) text = (en ? "Reminder: " : "Erinnerung: ") + e.name + at;
      else if (daysUntil == 1) text = (en ? "Tomorrow: " : "Morgen: ") + e.name + at;
      else text = en ? e.name + " in " + String(daysUntil) + " days" : "In " + String(daysUntil) + " Tagen: " + e.name;
      break;
    default:
      if (daysUntil == 0) text = (en ? "Today: " : "Heute: ") + e.name + at;
      else if (daysUntil == 1) text = (en ? "Tomorrow: " : "Morgen: ") + e.name + at;
      else text = en ? "In " + String(daysUntil) + " days: " + e.name : "In " + String(daysUntil) + " Tagen: " + e.name;
      break;
  }
  if (!e.description.isEmpty()) text += " - " + e.description;
  return text;
}

static uint8_t eventIcon(uint8_t type) {
  switch (type) {
    case EVENT_BIRTHDAY: return ICON_CAKE;
    case EVENT_WASTE: return ICON_TRASH;
    case EVENT_COUNTDOWN: return ICON_HOURGLASS;
    case EVENT_VACATION: return ICON_SUITCASE;
    case EVENT_REMINDER: return ICON_BELL;
    default: return ICON_CALENDAR;
  }
}

static void collectUpcoming(std::vector<Upcoming> &out, bool onlyShown, int32_t horizon) {
  int wday, minute;
  const int32_t today = localToday(&wday, &minute);
  if (today == cal::NO_DAY) return;
  for (const CalendarEvent &e : calendarEvents) {
    if (!e.enabled || (onlyShown && !e.show)) continue;
    const int32_t occurrence = cal::nextOccurrence(e.year, e.month, e.day, e.repeat, e.interval, today);
    if (occurrence == cal::NO_DAY) continue;
    const int32_t daysUntil = occurrence - today;
    if (daysUntil > (onlyShown ? static_cast<int32_t>(e.leadDays) : horizon)) continue;
    Upcoming item{occurrence, daysUntil, eventMessage(e, occurrence, daysUntil), eventIcon(e.type),
                  e.type == EVENT_WASTE || e.type == EVENT_APPOINTMENT || e.type == EVENT_REMINDER ? e.color : 0,
                  e.type == EVENT_BIRTHDAY && daysUntil == 0 && extras.birthdayConfetti, e.minute};
    out.push_back(item);
  }
  if (extras.holidays) {
    int y, m, d;
    cal::civilFromDays(today, y, m, d);
    cal::Holiday list[40];
    const int32_t lead = onlyShown ? extras.holidayLeadDays : horizon;
    for (int year = y; year <= y + 1; year++) {
      const size_t n = cal::germanHolidays(year, extras.holidayRegion.c_str(), extras.observances, list, 40);
      for (size_t i = 0; i < n; i++) {
        const int32_t daysUntil = list[i].day - today;
        if (daysUntil < 0 || daysUntil > lead) continue;
        const String name = english() ? list[i].nameEn : list[i].nameDe;
        String text;
        if (daysUntil == 0) text = english() ? "Today: " + name : "Heute ist " + name;
        else if (daysUntil == 1) text = english() ? "Tomorrow: " + name : "Morgen ist " + name;
        else text = english() ? String(daysUntil) + " days until " + name : "Noch " + String(daysUntil) + " Tage bis " + name;
        out.push_back(Upcoming{list[i].day, daysUntil, text, ICON_STAR, 0, false, -1});
      }
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const Upcoming &a, const Upcoming &b) {
    return a.daysUntil != b.daysUntil ? a.daysUntil < b.daysUntil : a.minute < b.minute;
  });
}

void describeUpcoming(JsonArray out, size_t max) {
  std::vector<Upcoming> items;
  collectUpcoming(items, false, 60);
  size_t n = 0;
  for (const Upcoming &item : items) {
    if (n++ >= max) break;
    int y, m, d;
    cal::civilFromDays(item.day, y, m, d);
    JsonObject o = out.add<JsonObject>();
    char date[16];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
    o["date"] = date;
    o["days"] = item.daysUntil;
    o["text"] = item.text;
  }
}

void describeHolidays(JsonArray out) {
  const int32_t today = localToday();
  if (today == cal::NO_DAY) return;
  int y, m, d;
  cal::civilFromDays(today, y, m, d);
  cal::Holiday list[40];
  const size_t n = cal::germanHolidays(y, extras.holidayRegion.c_str(), extras.observances, list, 40);
  for (size_t i = 0; i < n; i++) {
    int hy, hm, hd;
    cal::civilFromDays(list[i].day, hy, hm, hd);
    char date[16];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", hy, hm, hd);
    JsonObject o = out.add<JsonObject>();
    o["date"] = date;
    o["name"] = english() ? list[i].nameEn : list[i].nameDe;
    o["legal"] = list[i].legal;
  }
}

// ---- Playlists ---------------------------------------------------------------

static const char *const QUOTES_DE[] = {
  "Heute ist ein guter Tag!", "Übung macht den Meister.", "Gut Ding will Weile haben.",
  "Morgenstund hat Gold im Mund.", "Aller Anfang ist schwer.", "Wer rastet, der rostet.",
  "Lieber spät als nie.", "Kleine Schritte führen ans Ziel.", "Lächeln steckt an.",
  "Pause machen nicht vergessen!", "Trink genug Wasser!", "Du schaffst das!",
  "Genieße den Moment.", "Ordnung ist das halbe Leben.", "In der Ruhe liegt die Kraft.",
  "Ende gut, alles gut.", "Neuer Tag, neues Glück!", "Geteilte Freude ist doppelte Freude."
};
static const char *const QUOTES_EN[] = {
  "Today is a good day!", "Practice makes perfect.", "Good things take time.",
  "The early bird catches the worm.", "Every journey starts with a step.", "Better late than never.",
  "Small steps lead to big goals.", "Smile, it's contagious.", "Don't forget to take a break!",
  "Drink enough water!", "You've got this!", "Enjoy the moment.", "Keep calm and carry on.",
  "All's well that ends well.", "New day, new luck!", "Shared joy is double joy."
};

static String quoteOfTheDay() {
  std::vector<String> custom;
  int start = 0;
  const String &all = extras.quotes;
  while (start < static_cast<int>(all.length())) {
    int end = all.indexOf('\n', start);
    if (end < 0) end = all.length();
    String line = all.substring(start, end);
    line.trim();
    if (!line.isEmpty()) custom.push_back(line);
    start = end + 1;
  }
  const int32_t today = localToday();
  const uint32_t seed = extras.quoteRandom ? esp_random() : static_cast<uint32_t>(today == cal::NO_DAY ? 0 : today);
  if (!custom.empty()) return expandPlaceholders(custom[seed % custom.size()]);
  if (english()) return QUOTES_EN[seed % (sizeof(QUOTES_EN) / sizeof(QUOTES_EN[0]))];
  return QUOTES_DE[seed % (sizeof(QUOTES_DE) / sizeof(QUOTES_DE[0]))];
}

static String greetingText() {
  struct tm t;
  if (!getLocalTime(&t, 10)) return english() ? "Hello!" : "Hallo!";
  const int h = t.tm_hour;
  const bool en = english();
  String text = h >= 5 && h < 11 ? (en ? "Good morning" : "Guten Morgen")
              : h < 17 && h >= 11 ? (en ? "Good afternoon" : "Guten Tag")
              : h < 22 && h >= 17 ? (en ? "Good evening" : "Guten Abend")
              : (en ? "Good night" : "Gute Nacht");
  if (!extras.greetingName.isEmpty()) text += ", " + extras.greetingName;
  return text + "!";
}

static void buildWarnings(std::vector<Message> &out) {
  if (weather.lastFetch == 0 || isnan(weather.temperature)) return;
  const bool en = english();
  const float low = isnan(weather.temperatureMin) ? weather.temperature : weather.temperatureMin;
  const float high = isnan(weather.temperatureMax) ? weather.temperature : weather.temperatureMax;
  auto warn = [&](const String &text, uint32_t color) {
    Message m = infoMessage(text, ICON_WARNING, color);
    m.color = color;
    m.repeats = 2;
    out.push_back(m);
  };
  if (extras.warnThunder && weather.weatherCode >= 95) warn(en ? "Thunderstorm warning!" : "Gewitterwarnung!", 0xFFD000);
  if (extras.warnStorm) {
    const float wind = isnan(weather.windGust) ? weather.windSpeed : weather.windGust;
    if (!isnan(wind) && wind >= extras.stormLimit) {
      warn((en ? "Storm warning: gusts up to " : "Sturmwarnung: Böen bis ") + String(static_cast<int>(round(wind))) + " km/h", 0xFF6000);
    }
  }
  if (extras.warnFrost && !isnan(low) && low <= extras.frostLimit) {
    warn((en ? "Frost warning: low of " : "Frostwarnung: Tiefstwert ") + temperatureText(low), 0x60B0FF);
  }
  if (extras.warnHeat && !isnan(high) && high >= extras.heatLimit) {
    warn((en ? "Heat warning: up to " : "Hitzewarnung: bis ") + temperatureText(high), 0xFF3020);
  }
}

static String weatherInfoText() {
  const bool en = english();
  String text;
  auto add = [&](const String &part) {
    if (part.isEmpty()) return;
    if (!text.isEmpty()) text += "  \xC2\xB7  ";
    text += part;
  };
  if (extras.detailDescription && weather.weatherCode >= 0) add(weatherDescription(weather.weatherCode, en));
  add(temperatureText(weather.temperature));
  if (extras.detailFeels && !isnan(weather.feelsLike)) add((en ? "feels " : "gefühlt ") + temperatureText(weather.feelsLike));
  if (extras.detailMinMax && !isnan(weather.temperatureMin) && !isnan(weather.temperatureMax)) {
    add(temperatureText(weather.temperatureMin) + (en ? " to " : " bis ") + temperatureText(weather.temperatureMax));
  }
  if (extras.detailRain && !isnan(weather.precipProbability)) add((en ? "rain " : "Regen ") + String(static_cast<int>(round(weather.precipProbability))) + " %");
  if (extras.detailWind && !isnan(weather.windSpeed)) {
    String wind = (en ? "wind " : "Wind ") + String(static_cast<int>(round(weather.windSpeed))) + " km/h";
    if (!isnan(weather.windGust) && weather.windGust > weather.windSpeed + 5) {
      wind += en ? ", gusts " : ", Böen ";
      wind += String(static_cast<int>(round(weather.windGust)));
    }
    add(wind);
  }
  if (extras.detailHumidity && !isnan(weather.humidity)) add((en ? "humidity " : "Feuchte ") + String(static_cast<int>(round(weather.humidity))) + " %");
  return text;
}

static bool buildPlaylist(uint8_t type, std::vector<Message> &out) {
  out.clear();
  const bool en = english();
  switch (type) {
    case PAGE_TEXTS: {
      int wday, minute;
      const int32_t today = localToday(&wday, &minute);
      for (const ScrollText &text : scrollTexts) {
        if (!text.permanent && textScheduledNow(text, today, wday, minute)) out.push_back(messageFromText(text));
      }
      break;
    }
    case PAGE_EVENTS: {
      std::vector<Upcoming> items;
      collectUpcoming(items, true, 0);
      for (const Upcoming &item : items) {
        Message m = infoMessage(item.text, item.icon, item.color);
        m.confetti = item.confetti;
        out.push_back(m);
      }
      break;
    }
    case PAGE_WEATHERINFO:
      if (weather.lastFetch && !isnan(weather.temperature)) out.push_back(infoMessage(weatherInfoText(), ICON_WEATHER));
      break;
    case PAGE_WARNING:
      buildWarnings(out);
      break;
    case PAGE_WEEK: {
      struct tm t;
      if (getLocalTime(&t, 10)) {
        Message m = infoMessage((en ? "CW " : "KW ") + String(cal::isoWeek(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday)),
                                ICON_CALENDAR);
        m.font = textlayout::FONT_SMALL;  // fits next to the icon without scrolling
        out.push_back(m);
      }
      break;
    }
    case PAGE_SUN: {
      int16_t rise, set;
      if (sunTimesToday(rise, set)) {
        Message up = infoMessage(formatClock(rise), ICON_SUNRISE);
        up.font = textlayout::FONT_SMALL;
        up.staticMs = config.pageSeconds * 500UL;
        Message down = up;
        down.text = formatClock(set);
        down.icon = ICON_SUNSET;
        out.push_back(up);
        out.push_back(down);
      }
      break;
    }
    case PAGE_MOON: {
      const double age = cal::moonAge(static_cast<double>(time(nullptr)));
      const uint8_t phase = cal::moonPhaseIndex(age);
      out.push_back(infoMessage(String(cal::moonPhaseName(phase, en)) + " " + String(cal::moonIllumination(age)) + " %", ICON_MOON));
      break;
    }
    case PAGE_QUOTE:
      out.push_back(infoMessage(quoteOfTheDay()));
      break;
    case PAGE_NEWS:
      for (size_t i = 0; i < newsItems.size() && i < extras.newsCount; i++) out.push_back(infoMessage(newsItems[i], ICON_NEWS));
      break;
    case PAGE_GREETING:
      out.push_back(infoMessage(greetingText(), ICON_HEART));
      break;
    case PAGE_WIFI:
      if (WiFi.status() == WL_CONNECTED) {
        Message m = infoMessage(String(constrain(2 * (WiFi.RSSI() + 100), 0, 100)) + "%", ICON_WIFI);
        m.font = textlayout::FONT_SMALL;
        out.push_back(m);
      }
      break;
    default:
      break;
  }
  return !out.empty();
}

// ---- Animations --------------------------------------------------------------

static uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dUL;
  x ^= x >> 15;
  x *= 0x846ca68bUL;
  x ^= x >> 16;
  return x;
}

static void drawAnimation(uint32_t now) {
  const int w = config.width, h = config.height;
  switch (extras.animation) {
    case 1:  // sparkle
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          const uint32_t seed = hash32(x * 131 + y * 7919);
          const uint8_t wave = sin8(static_cast<uint8_t>(now / (6 + (seed & 7)) + (seed >> 8)));
          if (wave > 200) px(x, y, CHSV(seed >> 24, 160, (wave - 200) * 4));
        }
      }
      break;
    case 2:  // snow
      for (int x = 0; x < w; x++) {
        const uint32_t seed = hash32(x * 977);
        const int speed = 90 + (seed & 0x3f);
        const int span = h + 4 + ((seed >> 6) & 7);
        const int y = ((now / speed) + (seed >> 12)) % span;
        if (y < h) px(x, y, CRGB(200, 210, 255));
        const int y2 = ((now / (speed + 40)) + (seed >> 20)) % (span + 3);
        if (y2 < h) px((x + 3) % w, y2, CRGB(90, 100, 140));
      }
      break;
    case 3:  // matrix rain
      for (int x = 0; x < w; x++) {
        const uint32_t seed = hash32(x * 4241 + 17);
        const int speed = 60 + (seed & 0x3f);
        const int span = h + 10 + ((seed >> 6) & 7);
        const int head = ((now / speed) + (seed >> 10)) % span;
        for (int tail = 0; tail < 5; tail++) {
          const int y = head - tail;
          if (y >= 0 && y < h) px(x, y, tail == 0 ? CRGB(170, 255, 170) : CRGB(0, 200 >> tail, 0));
        }
      }
      break;
    default:  // rainbow wave
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) px(x, y, CHSV(static_cast<uint8_t>(x * 8 + y * 5 + now / 12), 230, 180));
      break;
  }
}

// ---- Page manager ------------------------------------------------------------

static bool isClassic(uint8_t type) {
  return type == PAGE_CLOCK || type == PAGE_DATE || type == PAGE_WEATHER || type == PAGE_ANIMATION;
}

static uint32_t classicDurationMs(const PageEntry &entry) {
  if (entry.seconds) return entry.seconds * 1000UL;
  if (entry.type == PAGE_CLOCK) return config.timePageSeconds * 1000UL;
  if (entry.type == PAGE_ANIMATION) return 10000UL;
  return config.pageSeconds * 1000UL;
}

static void drawClassic(uint8_t type, uint32_t now) {
  if (type == PAGE_ANIMATION) return drawAnimation(now);
  if (type == PAGE_CLOCK) drawClockPage();
  else if (type == PAGE_DATE) drawDatePage();
  else drawWeatherPage();
  if (config.displayMode != 2) {
    if (config.autoPage && pageEntries.size() > 1) drawIndicator(entryIndex, pageEntries.size());
    else if (!config.autoPage) drawIndicator(type, 3);
  }
}

// Enters the entry at `index`; returns false if that page has nothing to show.
static bool enterEntry(size_t index, uint32_t now) {
  const PageEntry &entry = pageEntries[index];
  entryIndex = index;
  entryStarted = now;
  activeType = entry.type;
  currentPage = entry.type;
  if (isClassic(entry.type)) return true;
  std::vector<Message> list;
  if (!buildPlaylist(entry.type, list)) return false;
  pagePlayer.load(std::move(list), now);
  return true;
}

static void advance(uint32_t now) {
  const size_t count = pageEntries.size();
  for (size_t step = 1; step <= count; step++) {
    if (enterEntry((entryIndex + step) % count, now)) return;
  }
  // Nothing has content: fall back to the clock.
  activeType = PAGE_CLOCK;
  currentPage = PAGE_CLOCK;
  entryStarted = now;
}

static bool updatePermanent(uint32_t now) {
  int wday, minute;
  const int32_t today = localToday(&wday, &minute);
  uint32_t signature = 0;
  for (size_t i = 0; i < scrollTexts.size(); i++) {
    if (scrollTexts[i].permanent && textScheduledNow(scrollTexts[i], today, wday, minute)) signature |= 1UL << i;
  }
  if (!signature) {
    permanentActive = false;
    permanentSignature = 0;
    return false;
  }
  if (signature != permanentSignature || permanentPlayer.done()) {
    std::vector<Message> list;
    for (size_t i = 0; i < scrollTexts.size(); i++) {
      if (signature & (1UL << i)) {
        Message m = messageFromText(scrollTexts[i]);
        m.maxMs = 0;
        list.push_back(m);
      }
    }
    permanentPlayer.load(std::move(list), now);
    permanentSignature = signature;
  }
  permanentActive = true;
  return true;
}

void pagesReset() {
  parsePageOrder(config.pageOrder, pageEntries);
  activeType = -1;
  entryIndex = 0;
  pagePlayer.clear();
  permanentPlayer.clear();
  permanentSignature = 0;
}

uint8_t pagesCurrentType() {
  return activeType < 0 ? static_cast<uint8_t>(PAGE_CLOCK) : static_cast<uint8_t>(activeType);
}

void pagesPreviewText(const ScrollText &text) {
  Message m = messageFromText(text);
  m.repeats = 1;
  m.maxMs = 30000;
  m.staticMs = 5000;
  std::vector<Message> list;
  list.push_back(m);
  previewPlayer.load(std::move(list), millis());
  previewUntil = millis() + 30000;
  lastRender = 0;
}

bool pagesAnimating() {
  if (!previewPlayer.done()) return previewPlayer.animating();
  if (permanentActive) return permanentPlayer.animating();
  if (activeType == PAGE_ANIMATION) return true;
  if (activeType >= 0 && !isClassic(activeType)) return pagePlayer.animating();
  return false;
}

bool pagesDraw(uint32_t now) {
  if (pageEntries.empty()) pagesReset();
  if (!previewPlayer.done()) {
    if (static_cast<int32_t>(now - previewUntil) < 0) {
      previewPlayer.draw(now);
      if (!previewPlayer.done()) return previewPlayer.animating();
    }
    previewPlayer.clear();
    entryStarted = now;  // the interrupted page starts over
    if (!isClassic(pagesCurrentType())) activeType = -1;
  }
  if (updatePermanent(now)) {
    permanentPlayer.draw(now);
    return permanentPlayer.animating();
  }
  if (!config.autoPage) {
    const uint8_t type = config.selectedPage < PAGE_TYPE_COUNT ? config.selectedPage : static_cast<uint8_t>(PAGE_CLOCK);
    if (activeType != type) {
      activeType = type;
      currentPage = type;
      entryStarted = now;
      pagePlayer.clear();
    }
    if (isClassic(type)) {
      drawClassic(type, now);
      return type == PAGE_ANIMATION;
    }
    if (pagePlayer.done()) {
      std::vector<Message> list;
      if (buildPlaylist(type, list)) pagePlayer.load(std::move(list), now);
    }
    if (pagePlayer.done()) {
      drawClassic(PAGE_CLOCK, now);
      return false;
    }
    pagePlayer.draw(now);
    return pagePlayer.animating();
  }
  if (activeType < 0) {
    entryIndex = pageEntries.size() - 1;
    advance(now);
  }
  const PageEntry &entry = pageEntries[entryIndex < pageEntries.size() ? entryIndex : 0];
  const uint32_t elapsed = now - entryStarted;
  bool finished;
  if (activeType != entry.type) finished = elapsed >= config.timePageSeconds * 1000UL;  // clock fallback
  else if (isClassic(entry.type)) finished = elapsed >= classicDurationMs(entry);
  else finished = pagePlayer.done() || (entry.seconds && elapsed >= entry.seconds * 1000UL);
  if (finished) {
    advance(now);
    lastPageSwitch = now;
  }
  if (activeType >= 0 && isClassic(activeType)) {
    drawClassic(activeType, now);
    return activeType == PAGE_ANIMATION;
  }
  pagePlayer.draw(now);
  if (pagePlayer.done()) {
    // The last message ended within this frame: show the next page right away.
    advance(now);
    if (isClassic(activeType)) drawClassic(activeType, now);
    else pagePlayer.draw(now);
  }
  return pagesAnimating();
}
