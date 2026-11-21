#include <cassert>
#include <iostream>
#include <string>
#include <algorithm>
#include <cmath>
#include <vector>
#include "../src/runtime_policy.h"
#include "../src/weather_decode.h"
#include "../src/cooperative_reader.h"
#include "../src/admin_auth.h"
#include "../src/weather_models.h"
#include "../src/location_query.h"
#include "../src/calendar_logic.h"
#include "../src/rss_parse.h"
#include "../src/text_layout.h"

struct TestClock {
  static inline uint32_t time = 0, pauses = 0;
  static uint32_t now() { return time; }
  static void pause() { ++time; ++pauses; }
  static void reset(uint32_t start = 0) { time = start; pauses = 0; }
};

struct TestClient {
  std::string data;
  size_t position = 0, chunkSize = 256, reads = 0;
  uint32_t readyAt = TestClock::now(), gapMs = 0, readCostMs = 0;
  bool keepOpen = false;
  int available() {
    return deadlineReached(TestClock::now(), readyAt)
        ? static_cast<int>(std::min(chunkSize, data.size() - position)) : 0;
  }
  int read(uint8_t *output, size_t count) {
    count = std::min(count, static_cast<size_t>(available()));
    memcpy(output, data.data() + position, count);
    position += count;
    ++reads;
    TestClock::time += readCostMs;
    readyAt = TestClock::now() + gapMs;
    return static_cast<int>(count);
  }
  bool connected() { return keepOpen || position < data.size(); }
};

using TestReader = CooperativeReader<TestClient, TestClock>;

static void testNetworkReader() {
  for (uint32_t start : {0U, UINT32_MAX - 4000}) {
    TestClock::reset(start);
    TestClient stalled{"{\"current\":{\"temperature_2m\":"};
    stalled.keepOpen = true;
    TestReader reader(stalled, 8000, 30000);
    JsonDocument doc;
    const auto error = deserializeJson(doc, reader);
    assert(error == DeserializationError::IncompleteInput);
    assert(TestClock::now() - start == 8000);
    // A stalled response must sleep every tick, not spin for the 8 s timeout.
    assert(TestClock::pauses >= 8000);
    stalled.data += "12}}";
    assert(reader.read() == -1); // A timed-out response cannot resume later.
  }

  TestClock::reset();
  TestClient slow{R"({"current":{"temperature_2m":12.5,"weather_code":2,"is_day":1}})"};
  slow.chunkSize = 1;
  slow.gapMs = 150;
  TestReader slowReader(slow, 8000, 30000);
  JsonDocument doc, filter;
  filterWeather(filter, WEATHER_PROVIDER_OPEN_METEO);
  assert(!deserializeJson(doc, slowReader, DeserializationOption::Filter(filter)));
  WeatherReading reading;
  decodeWeather(doc, WEATHER_PROVIDER_OPEN_METEO, reading);
  assert(reading.temperature == 12.5 && reading.weatherCode == 2);
  assert(TestClock::pauses > 5000); // Successful, but longer than the ESP watchdog.

  TestClock::reset(UINT32_MAX - 10000);
  const uint32_t start = TestClock::now();
  TestClient trickle{std::string(4000, ' ')};
  trickle.chunkSize = 1;
  trickle.gapMs = 100;
  TestReader bounded(trickle, 8000, 30000);
  assert(!bounded.find("missing"));
  assert(TestClock::now() - start == 30000); // Total limit, despite arriving data.
  assert(TestClock::pauses == 30000);

  TestClock::reset();
  TestClient met{std::string(250, ' ') + R"(""timeseries":[{"data":{"instant":{"details":{"air_temperature":6}}}}])"};
  met.readCostMs = 20;
  TestReader metReader(met, 8000, 30000);
  assert(metReader.find("\"timeseries\"") && metReader.find("["));
  filter.clear();
  filterWeather(filter, WEATHER_PROVIDER_MET_NORWAY);
  assert(!deserializeJson(doc, metReader, DeserializationOption::Filter(filter)));
  decodeWeather(doc, WEATHER_PROVIDER_MET_NORWAY, reading);
  assert(reading.temperature == 6 && TestClock::pauses >= 2);
  assert(met.reads == 2); // Buffered TLS reads, including a split search marker.

  TestClock::reset();
  TestClient disconnected{""};
  TestReader ended(disconnected, 8000, 30000);
  char buffer[8];
  assert(ended.readBytes(buffer, sizeof(buffer)) == 0 && TestClock::pauses == 0);
  TestClient bytes{"A\xff"};
  TestReader binary(bytes, 8000, 30000);
  assert(binary.readBytes(buffer, sizeof(buffer)) == 2);
  assert(buffer[0] == 'A' && static_cast<uint8_t>(buffer[1]) == 255);
}

static WeatherReading parse(const char *json, uint8_t provider) {
  JsonDocument doc, filter;
  filterWeather(filter, provider);
  assert(!deserializeJson(doc, json, DeserializationOption::Filter(filter)));
  WeatherReading result;
  decodeWeather(doc, provider, result);
  return result;
}

static void testBasicAuthorization() {
  uint8_t out[8];
  size_t written = 0;
  assert(decodeBase64("YWJj", 4, out, sizeof(out), written) && written == 3 && memcmp(out, "abc", 3) == 0);
  assert(decodeBase64("YQ==", 4, out, sizeof(out), written) && written == 1 && out[0] == 'a');
  assert(decodeBase64("YWI=", 4, out, sizeof(out), written) && written == 2);
  assert(!decodeBase64("YWJ", 3, out, sizeof(out), written));
  assert(!decodeBase64("Y=Jj", 4, out, sizeof(out), written));
  assert(!decodeBase64("YQ==YWJj", 8, out, sizeof(out), written));
  assert(!decodeBase64("YW*j", 4, out, sizeof(out), written));
  assert(!decodeBase64("YWJjYWJjYWJj", 12, out, 8, written));

  BasicCredentials credentials;
  // admin:test-pass:with:colons
  assert(parseBasicAuthorization("Basic YWRtaW46dGVzdC1wYXNzOndpdGg6Y29sb25z", credentials));
  assert(std::string(credentials.username) == "admin");
  assert(std::string(credentials.password) == "test-pass:with:colons");
  assert(parseBasicAuthorization("  bAsIc   YWRtaW46eA==  ", credentials) && std::string(credentials.password) == "x");
  assert(!parseBasicAuthorization("Bearer YWRtaW46eA==", credentials));
  assert(!parseBasicAuthorization("Basic YWRtaW4=", credentials));      // no colon
  assert(!parseBasicAuthorization("Basic YWQAbWluOng=", credentials));  // NUL byte
  assert(!parseBasicAuthorization(nullptr, credentials));
  const std::string longUser(MAX_ADMIN_USERNAME_LENGTH + 1, 'u');
  const std::string raw = longUser + ":x";
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  for (size_t i = 0; i < raw.size(); i += 3) {
    uint32_t chunk = static_cast<uint8_t>(raw[i]) << 16;
    if (i + 1 < raw.size()) chunk |= static_cast<uint8_t>(raw[i + 1]) << 8;
    if (i + 2 < raw.size()) chunk |= static_cast<uint8_t>(raw[i + 2]);
    encoded += alphabet[(chunk >> 18) & 63];
    encoded += alphabet[(chunk >> 12) & 63];
    encoded += i + 1 < raw.size() ? alphabet[(chunk >> 6) & 63] : '=';
    encoded += i + 2 < raw.size() ? alphabet[chunk & 63] : '=';
  }
  assert(!parseBasicAuthorization(("Basic " + encoded).c_str(), credentials));

  assert(constantTimeEquals("same", "same", 4));
  assert(!constantTimeEquals("same", "sane", 4));

  char suffix[7];
  const uint8_t mac[6] = {0x24, 0x6f, 0x28, 0xa1, 0x0b, 0xc3};
  formatDeviceSuffix(mac, suffix);
  assert(std::string(suffix) == "A10BC3");
}

static void testLoginThrottle() {
  LoginThrottle throttle;
  const uint32_t client = 0x0a00002a, other = 0x0a00002b;
  for (int i = 0; i < 4; ++i) throttle.recordFailure(client, 1000 + i);
  assert(throttle.retryAfterMs(client, 1010) == 0);
  throttle.recordFailure(client, 2000);  // fifth failure locks for 30 s
  assert(throttle.retryAfterMs(client, 2000) == 30000);
  assert(throttle.retryAfterMs(client, 31999) == 1);
  assert(throttle.retryAfterMs(client, 32000) == 0);
  assert(throttle.retryAfterMs(other, 2000) == 0);
  throttle.recordFailure(client, 40000);  // sixth doubles the lock
  assert(throttle.retryAfterMs(client, 40000) == 60000);
  for (int i = 0; i < 10; ++i) throttle.recordFailure(client, 50000);
  assert(throttle.retryAfterMs(client, 50000) == LoginThrottle::MAX_LOCK_MS);
  throttle.recordSuccess(client);
  assert(throttle.retryAfterMs(client, 50001) == 0);

  // Counters are forgotten 15 minutes after the last failure.
  for (int i = 0; i < 5; ++i) throttle.recordFailure(client, 100000);
  throttle.recordFailure(client, 100000 + LoginThrottle::FORGET_MS);
  assert(throttle.retryAfterMs(client, 100000 + LoginThrottle::FORGET_MS) == 0);

  // Wrapping millis() and slot reuse when more clients fail than slots exist.
  LoginThrottle wrapped;
  for (int i = 0; i < 5; ++i) wrapped.recordFailure(client, UINT32_MAX - 5000);
  assert(wrapped.retryAfterMs(client, 10000) == 30000 - 15001);
  for (uint32_t i = 0; i < LoginThrottle::SLOTS + 3; ++i) wrapped.recordFailure(0x0b000000 + i, 20000 + i);
  assert(wrapped.retryAfterMs(0x0b000000 + LoginThrottle::SLOTS + 2, 20100) == 0);
}

static void testPostalQuery() {
  PostalQuery q;
  assert(parsePostalQuery("01067", q) && std::string(q.code) == "01067" && !q.country[0]);
  assert(parsePostalQuery(" 01067 Dresden", q) && std::string(q.code) == "01067");
  assert(parsePostalQuery("Dresden, 01067", q) && std::string(q.code) == "01067");
  assert(parsePostalQuery("D-01067", q) && std::string(q.country) == "de");
  assert(parsePostalQuery("DE 10115", q) && std::string(q.code) == "10115" && std::string(q.country) == "de");
  assert(parsePostalQuery("AT-1010", q) && std::string(q.code) == "1010" && std::string(q.country) == "at");
  assert(parsePostalQuery("A-1010 Wien", q) && std::string(q.country) == "at");
  assert(parsePostalQuery("CH 3011", q) && std::string(q.country) == "ch");
  assert(parsePostalQuery("Ulm 89073", q) && !q.country[0]);
  assert(!parsePostalQuery("Berlin", q));
  assert(!parsePostalQuery("Frankfurt am Main", q));
  assert(!parsePostalQuery("123", q));
  assert(!parsePostalQuery("123456", q));
  assert(!parsePostalQuery("Ort-12345x", q));
}


static void testPageSequence() {
  // Pages 0..5; 2, 3 and 4 are empty (e.g. no warnings, no events, no texts).
  const bool content[] = {true, true, false, false, false, true};
  auto has = [&](size_t i) { return content[i]; };
  assert(nextPageWithContent(0, 6, has) == 1);
  assert(nextPageWithContent(1, 6, has) == 5);  // skips all empty pages, does not jump to 0
  assert(nextPageWithContent(5, 6, has) == 0);  // wraps around
  const bool none[] = {false, false, false};
  assert(nextPageWithContent(1, 3, [&](size_t i) { return none[i]; }) == 3);
  const bool onlyLast[] = {false, false, false, true};
  assert(nextPageWithContent(3, 4, [&](size_t i) { return onlyLast[i]; }) == 3);  // stays on the only page
  // Every entry is checked exactly once, in order.
  std::vector<size_t> visited;
  nextPageWithContent(2, 5, [&](size_t i) { visited.push_back(i); return false; });
  assert((visited == std::vector<size_t>{3, 4, 0, 1, 2}));
}

static void testCalendar() {
  using namespace cal;
  int y, m, d;
  for (int32_t day : {-1000, 0, 19000, 20760, 30000}) {
    civilFromDays(day, y, m, d);
    assert(daysFromCivil(y, m, d) == day);
  }
  const int32_t today = daysFromCivil(2026, 11, 3);
  assert(weekday(today) == 2);  // Tuesday
  assert(isoWeek(2026, 11, 3) == 45 && isoWeek(2021, 1, 3) == 53 && isoWeek(2024, 12, 30) == 1);
  assert(easterSunday(2025) == daysFromCivil(2025, 4, 20));
  assert(easterSunday(2026) == daysFromCivil(2026, 4, 5));
  assert(easterSunday(2027) == daysFromCivil(2027, 3, 28));

  Holiday list[40];
  auto has = [&](size_t n, int32_t day, const char *name) {
    for (size_t i = 0; i < n; i++) if (list[i].day == day && strcmp(list[i].nameDe, name) == 0) return true;
    return false;
  };
  size_t n = germanHolidays(2026, "BY", false, list, 40);
  assert(has(n, daysFromCivil(2026, 6, 4), "Fronleichnam") && has(n, daysFromCivil(2026, 1, 6), "Heilige Drei Könige"));
  assert(!has(n, daysFromCivil(2026, 10, 31), "Reformationstag"));
  n = germanHolidays(2026, "SN", true, list, 40);
  assert(has(n, daysFromCivil(2026, 11, 18), "Buß- und Bettag") && has(n, daysFromCivil(2026, 10, 31), "Reformationstag"));
  assert(has(n, daysFromCivil(2026, 11, 29), "1. Advent") && has(n, daysFromCivil(2026, 12, 20), "4. Advent"));
  assert(has(n, daysFromCivil(2026, 5, 10), "Muttertag") && has(n, daysFromCivil(2026, 2, 16), "Rosenmontag"));
  n = germanHolidays(2026, "", false, list, 40);
  assert(n == 11);  // nationwide days incl. Easter and Whit Sunday

  // Recurrences: the Christmas countdown example from the requirements.
  assert(nextOccurrence(2026, 12, 24, REPEAT_YEARLY, 1, today) - today == 51);
  assert(nextOccurrence(2026, 12, 24, REPEAT_YEARLY, 1, daysFromCivil(2026, 12, 23)) == daysFromCivil(2026, 12, 24));
  assert(nextOccurrence(2026, 12, 24, REPEAT_YEARLY, 1, daysFromCivil(2026, 12, 25)) == daysFromCivil(2027, 12, 24));
  assert(nextOccurrence(2000, 2, 29, REPEAT_YEARLY, 1, daysFromCivil(2027, 1, 1)) == daysFromCivil(2027, 2, 28));
  assert(nextOccurrence(2026, 1, 31, REPEAT_MONTHLY, 1, today) == daysFromCivil(2026, 11, 30));
  assert(nextOccurrence(2026, 1, 15, REPEAT_MONTHLY, 3, today) == daysFromCivil(2027, 1, 15));
  assert(nextOccurrence(2026, 10, 20, REPEAT_WEEKLY, 2, today) == daysFromCivil(2026, 11, 3));
  assert(nextOccurrence(2026, 10, 21, REPEAT_WEEKLY, 2, today) == daysFromCivil(2026, 11, 4));
  assert(nextOccurrence(2026, 10, 1, REPEAT_NONE, 1, today) == NO_DAY);
  assert(nextOccurrence(2027, 1, 1, REPEAT_WEEKLY, 1, today) == daysFromCivil(2027, 1, 1));

  // Berlin, summer solstice: sunrise 04:43 CEST (02:43 UTC), sunset 21:33 CEST.
  double rise, set;
  assert(sunEvent(2026, 6, 21, 52.52, 13.41, true, rise) && sunEvent(2026, 6, 21, 52.52, 13.41, false, set));
  assert(std::fabs(rise * 60 - (2 * 60 + 43)) < 5 && std::fabs(set * 60 - (19 * 60 + 33)) < 5);
  assert(!sunEvent(2026, 6, 21, 78.2, 15.6, true, rise));  // Svalbard: midnight sun
  // Full moon on 2026-01-03 10:03 UTC, new moon on 2026-01-18 19:52 UTC.
  assert(moonPhaseIndex(moonAge(1767434580.0)) == 4 && moonIllumination(moonAge(1767434580.0)) > 97);
  assert(moonPhaseIndex(moonAge(1768765920.0)) == 0);
}

static void testTextLayout() {
  using namespace textlayout;
  const char *text = "Grüße 5°C";
  size_t pos = 0, length = strlen(text);
  uint32_t cps[16];
  size_t n = 0;
  while (pos < length) cps[n++] = nextCodepoint(text, length, pos);
  assert(n == 9 && cps[2] == 0xFC && cps[3] == 0xDF && cps[7] == 0xB0);
  assert(largeColumns(0xFC) && largeColumns(0xC4) && largeColumns(0x20AC) && !largeColumns(0x4E2D));
  assert(textWidth("", FONT_LARGE) == 0 && textWidth("i", FONT_LARGE) < textWidth("W", FONT_LARGE));
  assert(textWidth("12:30", FONT_SMALL) == 17);
  assert(smallChar('a') == 'A' && smallChar(0xFC) == 'U' && smallChar(0x4E2D) == '?');
  int lit = 0;
  drawText("Hallo", 0, 0, FONT_LARGE, [&](int, int) { lit++; });
  assert(lit > 40);
  // Invalid UTF-8 must not read past the end.
  const char broken[] = {'A', static_cast<char>(0xE2), static_cast<char>(0x82), 0};
  pos = 0;
  nextCodepoint(broken, 3, pos);
  assert(nextCodepoint(broken, 3, pos) == '?' && pos <= 3);
}

static std::vector<std::string> headlines(const std::string &feed) {
  RssTitleParser parser;
  std::vector<std::string> out;
  for (char c : feed) if (parser.feed(c)) out.push_back(parser.title());
  return out;
}

static void testRss() {
  auto items = headlines(R"(<?xml version="1.0"?><rss><channel><title>Kanal</title>
    <item><title>Erste &amp; beste Meldung</title><link>x</link></item>
    <item><title><![CDATA[Zweite <b>Meldung</b> mit Ümlaut]]></title></item>
    <item><title>  Viel
      Leerraum &#8211; &#x263A; </title></item></channel></rss>)");
  assert(items.size() == 3);
  assert(items[0] == "Erste & beste Meldung");
  assert(items[1] == "Zweite Meldung mit \xC3\x9Cmlaut");
  assert(items[2] == "Viel Leerraum \xE2\x80\x93 \xE2\x98\xBA");
  auto atom = headlines(R"(<feed><title>Feed</title><entry><title type="html">Atom &lt;i&gt;Titel&lt;/i&gt;</title></entry></feed>)");
  assert(atom.size() == 1 && atom[0] == "Atom Titel");
  std::string longTitle = "<rss><item><title>" + std::string(400, 'x') + "</title></item></rss>";
  auto cut = headlines(longTitle);
  assert(cut.size() == 1 && cut[0].size() <= RssTitleParser::MAX_TITLE);
}

static void testWeatherDetails() {
  auto meteo = parse(R"({"current":{"temperature_2m":4,"apparent_temperature":1.2,"relative_humidity_2m":81,"weather_code":65,"is_day":1,"wind_speed_10m":18,"wind_gusts_10m":44},"daily":{"temperature_2m_min":[1],"temperature_2m_max":[7],"precipitation_probability_max":[90]}})", 0);
  assert(meteo.feelsLike == 1.2f && meteo.humidity == 81 && meteo.windSpeed == 18 && meteo.windGust == 44 && meteo.precipProbability == 90);
  auto owm = parse(R"({"main":{"temp":5,"feels_like":2,"humidity":70},"wind":{"speed":10,"gust":20},"weather":[{"id":500,"icon":"10d"}]})", 1);
  assert(owm.feelsLike == 2 && std::fabs(owm.windSpeed - 36) < 0.01 && std::fabs(owm.windGust - 72) < 0.01);
  auto dwd = parse(R"({"weather":{"temperature":3,"icon":"rain","relative_humidity":90,"wind_speed_10":25,"wind_gust_speed_10":50}})", 2);
  assert(dwd.humidity == 90 && dwd.windSpeed == 25 && dwd.windGust == 50 && dwd.feelsLike < 3);  // wind chill
  auto api = parse(R"({"current":{"temp_c":20,"is_day":1,"condition":{"code":1000},"feelslike_c":19,"humidity":40,"wind_kph":10,"gust_kph":15},"forecast":{"forecastday":[{"day":{"mintemp_c":12,"maxtemp_c":24,"daily_chance_of_rain":30,"daily_chance_of_snow":0}}]}})", 4);
  assert(api.precipProbability == 30 && api.windGust == 15 && api.feelsLike == 19);
  auto met = parse(R"({"data":{"instant":{"details":{"air_temperature":30,"relative_humidity":60,"wind_speed":2}},"next_1_hours":{"summary":{"symbol_code":"clearsky_day"}}}})", 3);
  assert(met.humidity == 60 && std::fabs(met.windSpeed - 7.2f) < 0.01 && met.feelsLike > 30);  // heat index
  auto minimal = parse(R"({"main":{"temp":12},"weather":[{"id":800,"icon":"01d"}]})", 1);
  assert(std::isnan(minimal.humidity) && std::isnan(minimal.windSpeed) && minimal.feelsLike == 12);
}

int main() {
  testPageSequence();
  testCalendar();
  testTextLayout();
  testRss();
  testWeatherDetails();
  testPostalQuery();
  testNetworkReader();
  testBasicAuthorization();
  testLoginThrottle();
  const uint32_t interval = 7200000, retry = 300000;
  assert(weatherFetchDue(1000, 0, 0, interval, retry, false));
  assert(!weatherFetchDue(9000, 5000, 5000, interval, retry, false));
  assert(weatherFetchDue(interval + 5000, 5000, 5000, interval, retry, false));
  // Regression: an expired good reading must not cause an unbounded failure loop.
  assert(!weatherFetchDue(interval + 6000, 5000, interval + 5000, interval, retry, true));
  assert(weatherFetchDue(interval + 5000 + retry, 5000, interval + 5000, interval, retry, true));
  assert(!weatherFetchDue(50000, 0, 1000, interval, retry, true));
  assert(!weatherFetchDue(5000, 10, UINT32_MAX - 1000, interval, retry, true));
  assert(weatherFetchDue(400000, 10, UINT32_MAX - 1000, interval, retry, true));
  assert(deadlineReached(20, UINT32_MAX - 10));
  assert(!deadlineReached(UINT32_MAX - 10, 20));

  auto meteo = parse(R"({"current":{"temperature_2m":-3.5,"weather_code":71,"is_day":0},"daily":{"temperature_2m_min":[-7],"temperature_2m_max":[2]},"unused":[1,2,3]})", 0);
  assert(meteo.temperature == -3.5 && meteo.temperatureMin == -7 && meteo.temperatureMax == 2 && !meteo.isDay && meteo.weatherCode == 71);
  auto owm = parse(R"({"main":{"temp":21,"temp_min":19,"temp_max":23},"weather":[{"id":802,"icon":"03n"}]})", 1);
  assert(owm.temperature == 21 && owm.temperatureMin == 19 && owm.weatherCode == 2 && !owm.isDay);
  auto dwd = parse(R"({"weather":{"temperature":0,"icon":"sleet"}})", 2);
  assert(dwd.temperature == 0 && dwd.weatherCode == 67 && std::isnan(dwd.temperatureMin));
  // Bright Sky "wind" and null icons fall back to condition and cloud cover.
  assert(parse(R"({"weather":{"temperature":9,"icon":"wind","condition":"dry","cloud_cover":90}})", 2).weatherCode == 3);
  assert(parse(R"({"weather":{"temperature":9,"icon":"wind","condition":"rain","cloud_cover":90}})", 2).weatherCode == 61);
  assert(parse(R"({"weather":{"temperature":9,"icon":null,"condition":"dry","cloud_cover":10}})", 2).weatherCode == 0);
  assert(parse(R"({"weather":{"temperature":9,"icon":null,"condition":null,"cloud_cover":50}})", 2).weatherCode == 2);
  auto night = parse(R"({"weather":{"temperature":4,"icon":"partly-cloudy-night","condition":"dry","cloud_cover":50}})", 2);
  assert(night.weatherCode == 2 && !night.isDay);
  assert(parse(R"({"weather":{"temperature":9,"icon":null,"condition":null,"cloud_cover":null}})", 2).weatherCode == -1);
  assert(normalizeBrightSkyCondition("hail", NAN) == 77);
  assert(isOpenMeteoModel("") && isOpenMeteoModel("icon_seamless") && isOpenMeteoModel("jma_seamless"));
  assert(!isOpenMeteoModel("bom_access_global") && !isOpenMeteoModel("icon_seamless&x=1"));
  auto met = parse(R"({"time":"2026-09-11T10:00:00Z","data":{"instant":{"details":{"air_temperature":12.5}},"next_1_hours":{"summary":{"symbol_code":"lightrainshowers_night"}}}})", 3);
  assert(met.temperature == 12.5 && met.weatherCode == 80 && !met.isDay && std::isnan(met.temperatureMax));
  auto fallback = parse(R"({"data":{"instant":{"details":{"air_temperature":6}},"next_6_hours":{"summary":{"symbol_code":"snow"}}}})", 3);
  assert(fallback.temperature == 6 && fallback.weatherCode == 71);
  auto wa = parse(R"({"current":{"temp_c":18.2,"is_day":1,"condition":{"code":1276}},"forecast":{"forecastday":[{"day":{"mintemp_c":8,"maxtemp_c":22},"hour":[{"temp_c":999}]}]}})", 4);
  assert(wa.temperature > 18 && wa.temperatureMin == 8 && wa.temperatureMax == 22 && wa.weatherCode == 95 && wa.isDay);
  for (uint8_t provider = 0; provider <= WEATHER_PROVIDER_MAX; ++provider) {
    assert(std::isnan(parse("{}", provider).temperature));
  }
  assert(std::isnan(parse(R"({"current":{"temp_c":null}})", 4).temperature));
  assert(normalizeMetSymbol("unknown") == -1);
  assert(normalizeMetSymbol(nullptr) == -1);
  assert(normalizeMetSymbol("heavysleetandthunder") == 95);
  assert(normalizeMetSymbol("partlycloudy_polartwilight") == 2);
  assert(normalizeWeatherApiCode(9999) == -1);
  assert(normalizeWeatherApiCode(1000) == 0);
  assert(normalizeWeatherApiCode(1168) == 57);
  assert(normalizeWeatherApiCode(1258) == 85);
  std::cout << "Firmware regression tests passed (page sequence, calendar, holidays, sun/moon, text layout, RSS, weather details, cooperative network reads, deadlines, scheduling, rollover, five providers, missing/null data, condition mapping, Basic auth parsing, login throttling).\n";
}
