#pragma once
#include <ArduinoJson.h>
#include "weather_data.h"
#include "weather_codes.h"

inline void filterWeather(JsonDocument &filter, uint8_t provider) {
  if (provider == WEATHER_PROVIDER_OPEN_WEATHER_MAP) {
    for (const char *key : {"temp", "temp_min", "temp_max", "feels_like", "humidity"}) filter["main"][key] = true;
    filter["wind"]["speed"] = true;
    filter["wind"]["gust"] = true;
    filter["weather"][0]["id"] = true;
    filter["weather"][0]["icon"] = true;
  } else if (provider == WEATHER_PROVIDER_DWD) {
    filter["weather"]["temperature"] = true;
    filter["weather"]["icon"] = true;
    filter["weather"]["condition"] = true;
    filter["weather"]["cloud_cover"] = true;
    filter["weather"]["relative_humidity"] = true;
    filter["weather"]["wind_speed_10"] = true;
    filter["weather"]["wind_gust_speed_10"] = true;
  } else if (provider == WEATHER_PROVIDER_WEATHER_API) {
    filter["current"]["temp_c"] = true;
    filter["current"]["is_day"] = true;
    filter["current"]["condition"]["code"] = true;
    for (const char *key : {"feelslike_c", "humidity", "wind_kph", "gust_kph"}) filter["current"][key] = true;
    for (const char *key : {"mintemp_c", "maxtemp_c", "daily_chance_of_rain", "daily_chance_of_snow"}) {
      filter["forecast"]["forecastday"][0]["day"][key] = true;
    }
  } else if (provider == WEATHER_PROVIDER_MET_NORWAY) {
    filter["data"]["instant"]["details"]["air_temperature"] = true;
    filter["data"]["instant"]["details"]["relative_humidity"] = true;
    filter["data"]["instant"]["details"]["wind_speed"] = true;
    filter["data"]["next_1_hours"]["summary"]["symbol_code"] = true;
    filter["data"]["next_6_hours"]["summary"]["symbol_code"] = true;
  } else {
    for (const char *key : {"temperature_2m", "weather_code", "is_day", "apparent_temperature",
                            "relative_humidity_2m", "wind_speed_10m", "wind_gusts_10m"}) filter["current"][key] = true;
    filter["daily"]["temperature_2m_max"][0] = true;
    filter["daily"]["temperature_2m_min"][0] = true;
    filter["daily"]["precipitation_probability_max"][0] = true;
  }
}

inline void decodeWeather(JsonDocument &doc, uint8_t provider, WeatherReading &sample) {
  sample.temperatureMin = NAN;
  sample.temperatureMax = NAN;
  sample.feelsLike = NAN;
  sample.humidity = NAN;
  sample.windSpeed = NAN;
  sample.windGust = NAN;
  sample.precipProbability = NAN;
  if (provider == WEATHER_PROVIDER_OPEN_WEATHER_MAP) {
    sample.temperature = doc["main"]["temp"] | NAN;
    sample.temperatureMin = doc["main"]["temp_min"] | NAN;
    sample.temperatureMax = doc["main"]["temp_max"] | NAN;
    sample.weatherCode = normalizeOpenWeatherCode(doc["weather"][0]["id"] | -1);
    const char *icon = doc["weather"][0]["icon"] | "";
    sample.isDay = strlen(icon) < 3 || icon[2] != 'n';
    sample.feelsLike = doc["main"]["feels_like"] | NAN;
    sample.humidity = doc["main"]["humidity"] | NAN;
    sample.windSpeed = (doc["wind"]["speed"] | NAN) * 3.6f;
    sample.windGust = (doc["wind"]["gust"] | NAN) * 3.6f;
  } else if (provider == WEATHER_PROVIDER_DWD) {
    sample.temperature = doc["weather"]["temperature"] | NAN;
    const char *icon = doc["weather"]["icon"] | "";
    sample.weatherCode = normalizeBrightSkyIcon(icon);
    if (sample.weatherCode < 0) {
      sample.weatherCode = normalizeBrightSkyCondition(doc["weather"]["condition"] | "", doc["weather"]["cloud_cover"] | NAN);
    }
    sample.isDay = strstr(icon, "night") == nullptr;
    sample.humidity = doc["weather"]["relative_humidity"] | NAN;
    sample.windSpeed = doc["weather"]["wind_speed_10"] | NAN;
    sample.windGust = doc["weather"]["wind_gust_speed_10"] | NAN;
  } else if (provider == WEATHER_PROVIDER_WEATHER_API) {
    sample.temperature = doc["current"]["temp_c"] | NAN;
    sample.temperatureMin = doc["forecast"]["forecastday"][0]["day"]["mintemp_c"] | NAN;
    sample.temperatureMax = doc["forecast"]["forecastday"][0]["day"]["maxtemp_c"] | NAN;
    sample.weatherCode = normalizeWeatherApiCode(doc["current"]["condition"]["code"] | -1);
    sample.isDay = (doc["current"]["is_day"] | 1) == 1;
    sample.feelsLike = doc["current"]["feelslike_c"] | NAN;
    sample.humidity = doc["current"]["humidity"] | NAN;
    sample.windSpeed = doc["current"]["wind_kph"] | NAN;
    sample.windGust = doc["current"]["gust_kph"] | NAN;
    const float rain = doc["forecast"]["forecastday"][0]["day"]["daily_chance_of_rain"] | NAN;
    const float snow = doc["forecast"]["forecastday"][0]["day"]["daily_chance_of_snow"] | NAN;
    sample.precipProbability = isnan(snow) ? rain : isnan(rain) ? snow : (rain > snow ? rain : snow);
  } else if (provider == WEATHER_PROVIDER_MET_NORWAY) {
    sample.temperature = doc["data"]["instant"]["details"]["air_temperature"] | NAN;
    const char *symbol = doc["data"]["next_1_hours"]["summary"]["symbol_code"] |
      (doc["data"]["next_6_hours"]["summary"]["symbol_code"] | "");
    sample.weatherCode = normalizeMetSymbol(symbol);
    sample.isDay = strstr(symbol, "night") == nullptr;
    sample.humidity = doc["data"]["instant"]["details"]["relative_humidity"] | NAN;
    sample.windSpeed = (doc["data"]["instant"]["details"]["wind_speed"] | NAN) * 3.6f;
  } else {
    sample.temperature = doc["current"]["temperature_2m"] | NAN;
    sample.temperatureMax = doc["daily"]["temperature_2m_max"][0] | NAN;
    sample.temperatureMin = doc["daily"]["temperature_2m_min"][0] | NAN;
    sample.weatherCode = doc["current"]["weather_code"] | -1;
    sample.isDay = (doc["current"]["is_day"] | 1) == 1;
    sample.feelsLike = doc["current"]["apparent_temperature"] | NAN;
    sample.humidity = doc["current"]["relative_humidity_2m"] | NAN;
    sample.windSpeed = doc["current"]["wind_speed_10m"] | NAN;
    sample.windGust = doc["current"]["wind_gusts_10m"] | NAN;
    sample.precipProbability = doc["daily"]["precipitation_probability_max"][0] | NAN;
  }
  if (isnan(sample.feelsLike)) sample.feelsLike = apparentTemperature(sample.temperature, sample.humidity, sample.windSpeed);
}

