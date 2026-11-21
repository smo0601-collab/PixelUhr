#pragma once
#include <stdint.h>
#include <math.h>

constexpr uint8_t WEATHER_PROVIDER_OPEN_METEO = 0;
constexpr uint8_t WEATHER_PROVIDER_OPEN_WEATHER_MAP = 1;
constexpr uint8_t WEATHER_PROVIDER_DWD = 2;
constexpr uint8_t WEATHER_PROVIDER_MET_NORWAY = 3;
constexpr uint8_t WEATHER_PROVIDER_WEATHER_API = 4;
constexpr uint8_t WEATHER_PROVIDER_MAX = WEATHER_PROVIDER_WEATHER_API;

struct WeatherReading {
  float temperature = NAN;
  float temperatureMax = NAN;
  float temperatureMin = NAN;
  int weatherCode = -1;
  bool isDay = true;
  // Optional details; NAN when the provider does not deliver them.
  float feelsLike = NAN;          // degrees Celsius
  float humidity = NAN;           // percent
  float windSpeed = NAN;          // km/h
  float windGust = NAN;           // km/h
  float precipProbability = NAN;  // percent, today
};

// Apparent temperature from wind chill (cold) or heat index (warm) when the
// provider sends no "feels like" value.
inline float apparentTemperature(float t, float humidity, float windKmh) {
  if (isnan(t)) return NAN;
  if (t <= 10.0f && !isnan(windKmh) && windKmh > 4.8f) {
    const float v = powf(windKmh, 0.16f);
    return 13.12f + 0.6215f * t - 11.37f * v + 0.3965f * t * v;
  }
  if (t >= 27.0f && !isnan(humidity) && humidity >= 40.0f) {
    const float f = t * 9.0f / 5.0f + 32.0f, r = humidity;
    const float hi = -42.379f + 2.04901523f * f + 10.14333127f * r - 0.22475541f * f * r -
                     0.00683783f * f * f - 0.05481717f * r * r + 0.00122874f * f * f * r +
                     0.00085282f * f * r * r - 0.00000199f * f * f * r * r;
    return (hi - 32.0f) * 5.0f / 9.0f;
  }
  return t;
}
