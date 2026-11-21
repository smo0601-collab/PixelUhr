#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "app_state.h"
#include "cooperative_reader.h"
#include "features.h"
#include "rss_parse.h"

// News headlines from an RSS or Atom feed. Runs in the network worker task;
// the feed is parsed while streaming, only the headlines are kept.

namespace {

struct RtosClock {
  static uint32_t now() { return millis(); }
  static void pause() { vTaskDelay(1); }
};

constexpr uint32_t NEWS_RETRY_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t NEWS_BODY_TIMEOUT_MS = 25000;
constexpr size_t NEWS_MAX_BYTES = 400000;
uint32_t lastNewsAttempt = 0;

}  // namespace

bool newsFetchDue(uint32_t now) {
  if (extras.newsUrl.isEmpty()) return false;
  if (pendingNewsFetch) return true;
  if (!newsError.isEmpty()) return now - lastNewsAttempt >= NEWS_RETRY_MS;
  return newsFetchedAt == 0 || now - newsFetchedAt >= extras.newsIntervalMinutes * 60000UL;
}

template <class Client>
static bool readHeadlines(HTTPClient &http, Client &, std::vector<String> &items, uint8_t limit) {
  WiFiClient *stream = http.getStreamPtr();
  if (!stream) return false;
  CooperativeReader<WiFiClient, RtosClock> reader(*stream, HTTP_TIMEOUT_MS, NEWS_BODY_TIMEOUT_MS);
  RssTitleParser parser;
  size_t bytes = 0;
  for (int c; items.size() < limit && bytes < NEWS_MAX_BYTES && (c = reader.read()) >= 0; bytes++) {
    if (parser.feed(static_cast<char>(c))) items.push_back(String(parser.title()));
  }
  return !items.empty();
}

void fetchNews() {
  String url;
  uint8_t limit;
  uint32_t revision;
  {
    StateLock lock;
    if (WiFi.status() != WL_CONNECTED || extras.newsUrl.isEmpty()) return;
    url = extras.newsUrl;
    limit = extras.newsCount;
    revision = newsRevision;
    pendingNewsFetch = false;
    lastNewsAttempt = millis();
  }
  std::vector<String> items;
  String error;
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setUserAgent("PixelUhr/" FIRMWARE_VERSION_TEXT);
  const bool secure = url.startsWith("https://");
  WiFiClientSecure tls;
  WiFiClient plain;
  bool begun;
  if (secure) {
    // Headlines are public, read-only data from a feed the owner chose; the
    // certificate is not pinned so that any news site works.
    tls.setInsecure();
    tls.setHandshakeTimeout(12);
    tls.setTimeout((HTTP_TIMEOUT_MS + 999) / 1000);
    begun = http.begin(tls, url);
  } else {
    begun = http.begin(plain, url);
  }
  if (!begun) {
    error = "Ungültige Adresse";
  } else {
    http.addHeader("Accept-Encoding", "identity");
    const int code = http.GET();
    if (code < 0) error = "Verbindung fehlgeschlagen (" + HTTPClient::errorToString(code) + ")";
    else if (code != HTTP_CODE_OK) error = "HTTP " + String(code);
    else if (!(secure ? readHeadlines(http, tls, items, limit) : readHeadlines(http, plain, items, limit))) {
      error = "Keine Schlagzeilen gefunden";
    }
    http.end();
  }
  StateLock lock;
  if (revision != newsRevision) return;
  if (!error.isEmpty()) {
    newsError = error;
    Serial.printf("News: %s\n", error.c_str());
    return;
  }
  newsItems.swap(items);
  newsError = "";
  newsFetchedAt = millis();
}
