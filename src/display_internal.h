#pragma once
// Drawing primitives shared by display.cpp, pages.cpp and alarm.cpp.
#include <Arduino.h>
#include <FastLED.h>
#include <time.h>

void px(int x, int y, CRGB color);
CRGB textPixelColor(int x, int y, CRGB fallback);
void drawBitmapIcon(int x, int y, const uint32_t *icon);
void drawWeatherIcon(int x, int y);
const uint32_t *currentWeatherIcon();
void drawClockPage();
void drawDatePage();
void drawWeatherPage();
void drawIndicator(uint8_t active, uint8_t count);
void drawText3x5(int x, int y, const String &text, CRGB color);
void drawCenteredText3x5(int y, const String &text, CRGB color);
String weatherDescription(int code, bool english);

// Icons for messages (pages.cpp).
enum MessageIcon : uint8_t {
  ICON_NONE = 0,
  ICON_CALENDAR,
  ICON_CAKE,
  ICON_TRASH,
  ICON_HOURGLASS,
  ICON_STAR,
  ICON_SUITCASE,
  ICON_WARNING,
  ICON_BELL,
  ICON_NEWS,
  ICON_SUNRISE,
  ICON_SUNSET,
  ICON_MOON,
  ICON_WIFI,
  ICON_WEATHER,
  ICON_HEART
};

constexpr int MESSAGE_ICON_W = 9;
void drawMessageIcon(uint8_t icon, int x, int y, uint32_t color);
// Draws UTF-8 text clipped to [clipLeft, clipRight). Returns the end x.
int drawTextClipped(const char *text, int x, int y, uint8_t font, CRGB color, int clipLeft, int clipRight,
                    bool rainbow);
int textPixelWidth(const char *text, uint8_t font);
int textTopY(uint8_t font);
