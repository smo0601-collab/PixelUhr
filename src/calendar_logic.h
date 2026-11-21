#pragma once
// Portable date, holiday, sun and moon calculations. No Arduino dependencies;
// covered by tests/firmware-tests.cpp.
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace cal {

constexpr int32_t NO_DAY = INT32_MIN;

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's algorithm).
inline int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

inline void civilFromDays(int32_t z, int &y, int &m, int &d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yoe) + era * 400 + (m <= 2);
}

// 0 = Sunday ... 6 = Saturday (struct tm convention).
inline int weekday(int32_t days) {
  const int w = static_cast<int>((days + 4) % 7);
  return w < 0 ? w + 7 : w;
}

inline bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

inline int daysInMonth(int y, int m) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && isLeap(y) ? 29 : days[(m - 1) % 12];
}

// ISO 8601 calendar week.
inline int isoWeek(int y, int m, int d) {
  const int32_t day = daysFromCivil(y, m, d);
  const int wd = (weekday(day) + 6) % 7;  // Monday = 0
  const int32_t thursday = day - wd + 3;
  int ty, tm, td;
  civilFromDays(thursday, ty, tm, td);
  return static_cast<int>((thursday - daysFromCivil(ty, 1, 1)) / 7 + 1);
}

// Easter Sunday (anonymous Gregorian algorithm).
inline int32_t easterSunday(int y) {
  const int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4;
  const int f = (b + 8) / 25, g = (b - f + 1) / 3, h = (19 * a + b - d - g + 15) % 30;
  const int i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
  const int m = (a + 11 * h + 22 * l) / 451;
  const int month = (h + l - 7 * m + 114) / 31;
  const int day = ((h + l - 7 * m + 114) % 31) + 1;
  return daysFromCivil(y, month, day);
}

struct Holiday {
  int32_t day;
  const char *nameDe;
  const char *nameEn;
  bool legal;
};

inline bool regionIn(const char *region, const char *list) {
  if (!region || strlen(region) != 2) return false;
  for (const char *p = list; *p; p += (p[2] ? 3 : 2)) {
    if (p[0] == region[0] && p[1] == region[1]) return true;
    if (!p[2]) break;
  }
  return false;
}

// German public holidays for `region` (Bundesland code such as "BY"; empty for
// nationwide days only) plus well-known observances when `observances` is set.
inline size_t germanHolidays(int year, const char *region, bool observances, Holiday *out, size_t max) {
  size_t n = 0;
  auto add = [&](int32_t day, const char *de, const char *en, bool legal) {
    if (n < max) out[n++] = Holiday{day, de, en, legal};
  };
  const int32_t easter = easterSunday(year);
  add(daysFromCivil(year, 1, 1), "Neujahr", "New Year's Day", true);
  if (regionIn(region, "BW,BY,ST")) add(daysFromCivil(year, 1, 6), "Heilige Drei Könige", "Epiphany", true);
  if (observances) add(daysFromCivil(year, 2, 14), "Valentinstag", "Valentine's Day", false);
  if (observances) add(easter - 48, "Rosenmontag", "Rose Monday", false);
  if (regionIn(region, "BE,MV")) add(daysFromCivil(year, 3, 8), "Frauentag", "Women's Day", true);
  add(easter - 2, "Karfreitag", "Good Friday", true);
  add(easter, "Ostersonntag", "Easter Sunday", regionIn(region, "BB"));
  add(easter + 1, "Ostermontag", "Easter Monday", true);
  add(daysFromCivil(year, 5, 1), "Tag der Arbeit", "Labour Day", true);
  if (observances) {
    const int32_t may1 = daysFromCivil(year, 5, 1);
    const int32_t firstSunday = may1 + (7 - weekday(may1)) % 7;
    add(firstSunday + 7, "Muttertag", "Mother's Day", false);
  }
  add(easter + 39, "Christi Himmelfahrt", "Ascension Day", true);
  add(easter + 49, "Pfingstsonntag", "Whit Sunday", regionIn(region, "BB"));
  add(easter + 50, "Pfingstmontag", "Whit Monday", true);
  if (regionIn(region, "BW,BY,HE,NW,RP,SL")) add(easter + 60, "Fronleichnam", "Corpus Christi", true);
  if (regionIn(region, "BY,SL")) add(daysFromCivil(year, 8, 15), "Mariä Himmelfahrt", "Assumption Day", true);
  if (regionIn(region, "TH")) add(daysFromCivil(year, 9, 20), "Weltkindertag", "World Children's Day", true);
  add(daysFromCivil(year, 10, 3), "Tag der Deutschen Einheit", "German Unity Day", true);
  if (regionIn(region, "BB,HB,HH,MV,NI,SN,ST,SH,TH")) add(daysFromCivil(year, 10, 31), "Reformationstag", "Reformation Day", true);
  if (regionIn(region, "BW,BY,NW,RP,SL")) add(daysFromCivil(year, 11, 1), "Allerheiligen", "All Saints' Day", true);
  if (regionIn(region, "SN")) {
    const int32_t nov23 = daysFromCivil(year, 11, 23);
    const int back = (weekday(nov23) - 3 + 7) % 7;
    add(nov23 - (back == 0 ? 7 : back), "Buß- und Bettag", "Day of Repentance", true);
  }
  if (observances) {
    const int32_t dec24 = daysFromCivil(year, 12, 24);
    const int32_t fourthAdvent = dec24 - weekday(dec24);
    add(fourthAdvent - 21, "1. Advent", "1st Advent", false);
    add(fourthAdvent - 14, "2. Advent", "2nd Advent", false);
    add(daysFromCivil(year, 12, 6), "Nikolaus", "St. Nicholas Day", false);
    add(fourthAdvent - 7, "3. Advent", "3rd Advent", false);
    add(fourthAdvent, "4. Advent", "4th Advent", false);
    add(dec24, "Heiligabend", "Christmas Eve", false);
  }
  add(daysFromCivil(year, 12, 25), "1. Weihnachtstag", "Christmas Day", true);
  add(daysFromCivil(year, 12, 26), "2. Weihnachtstag", "Boxing Day", true);
  if (observances) add(daysFromCivil(year, 12, 31), "Silvester", "New Year's Eve", false);
  return n;
}

// Event recurrence: 0 none, 1 yearly, 2 monthly, 3 weekly. `interval` repeats
// every n months/weeks. Returns the first occurrence on or after `today`.
constexpr uint8_t REPEAT_NONE = 0;
constexpr uint8_t REPEAT_YEARLY = 1;
constexpr uint8_t REPEAT_MONTHLY = 2;
constexpr uint8_t REPEAT_WEEKLY = 3;

inline int32_t nextOccurrence(int sy, int sm, int sd, uint8_t repeat, uint8_t interval, int32_t today) {
  const int32_t start = daysFromCivil(sy, sm, sd);
  if (start >= today || repeat == REPEAT_NONE) return start >= today ? start : NO_DAY;
  if (interval == 0) interval = 1;
  int ty, tm, td;
  civilFromDays(today, ty, tm, td);
  if (repeat == REPEAT_YEARLY) {
    for (int y = ty; y <= ty + 1; y++) {
      const int day = (sm == 2 && sd == 29 && !isLeap(y)) ? 28 : sd;
      const int32_t candidate = daysFromCivil(y, sm, day);
      if (candidate >= today) return candidate;
    }
    return NO_DAY;
  }
  if (repeat == REPEAT_MONTHLY) {
    const int startIndex = sy * 12 + (sm - 1);
    int index = ty * 12 + (tm - 1);
    int offset = index - startIndex;
    if (offset % interval) offset += interval - offset % interval;
    for (int i = 0; i < 3; i++, offset += interval) {
      const int month = startIndex + offset;
      const int y = month / 12, m = month % 12 + 1;
      const int day = sd > daysInMonth(y, m) ? daysInMonth(y, m) : sd;
      const int32_t candidate = daysFromCivil(y, m, day);
      if (candidate >= today) return candidate;
    }
    return NO_DAY;
  }
  if (repeat == REPEAT_WEEKLY) {
    const int32_t period = 7 * static_cast<int32_t>(interval);
    const int32_t steps = (today - start + period - 1) / period;
    return start + steps * period;
  }
  return NO_DAY;
}

// Sunrise/sunset in UTC hours for a date ("Almanac for Computers", zenith
// 90.833 degrees). Returns false for polar day/night.
inline bool sunEvent(int y, int m, int d, double lat, double lon, bool rise, double &utcHours) {
  const double rad = M_PI / 180.0;
  const int n = static_cast<int>(daysFromCivil(y, m, d) - daysFromCivil(y, 1, 1)) + 1;
  const double lngHour = lon / 15.0;
  const double t = n + ((rise ? 6.0 : 18.0) - lngHour) / 24.0;
  const double meanAnomaly = 0.9856 * t - 3.289;
  double l = meanAnomaly + 1.916 * sin(meanAnomaly * rad) + 0.020 * sin(2 * meanAnomaly * rad) + 282.634;
  l = fmod(l + 360.0, 360.0);
  double ra = atan(0.91764 * tan(l * rad)) / rad;
  ra = fmod(ra + 360.0, 360.0);
  ra += floor(l / 90.0) * 90.0 - floor(ra / 90.0) * 90.0;
  ra /= 15.0;
  const double sinDec = 0.39782 * sin(l * rad);
  const double cosDec = cos(asin(sinDec));
  const double cosH = (cos(90.833 * rad) - sinDec * sin(lat * rad)) / (cosDec * cos(lat * rad));
  if (cosH > 1.0 || cosH < -1.0) return false;
  double h = rise ? 360.0 - acos(cosH) / rad : acos(cosH) / rad;
  h /= 15.0;
  const double localMean = h + ra - 0.06571 * t - 6.622;
  utcHours = fmod(localMean - lngHour + 48.0, 24.0);
  return true;
}

constexpr double SYNODIC_MONTH = 29.530588853;

// Moon age in days (0 = new moon) for a Unix time.
inline double moonAge(double unixSeconds) {
  const double julian = unixSeconds / 86400.0 + 2440587.5;
  double age = fmod(julian - 2451550.1, SYNODIC_MONTH);
  if (age < 0) age += SYNODIC_MONTH;
  return age;
}

// 0 new, 1 waxing crescent, 2 first quarter, 3 waxing gibbous, 4 full,
// 5 waning gibbous, 6 last quarter, 7 waning crescent.
inline uint8_t moonPhaseIndex(double age) {
  return static_cast<uint8_t>(static_cast<int>(floor(age / SYNODIC_MONTH * 8.0 + 0.5)) % 8);
}

inline uint8_t moonIllumination(double age) {
  return static_cast<uint8_t>(lround((1.0 - cos(2.0 * M_PI * age / SYNODIC_MONTH)) * 50.0));
}

inline const char *moonPhaseName(uint8_t index, bool english) {
  static const char *de[] = {"Neumond", "Zunehmende Sichel", "Erstes Viertel", "Zunehmender Mond",
                             "Vollmond", "Abnehmender Mond", "Letztes Viertel", "Abnehmende Sichel"};
  static const char *en[] = {"New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
                             "Full moon", "Waning gibbous", "Last quarter", "Waning crescent"};
  return (english ? en : de)[index % 8];
}

}  // namespace cal
