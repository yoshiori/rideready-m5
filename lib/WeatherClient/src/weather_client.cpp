#include "weather_client.h"

#include <ArduinoJson.h>

bool WeatherClient::parseWeather(const char* json, WeatherData& out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;

  JsonObject current = doc["current"];
  if (current.isNull()) return false;

  out.temperature_c = current["temperature_2m"].as<float>();
  out.wind_speed_kmh = current["wind_speed_10m"].as<float>();
  out.wind_direction_deg = current["wind_direction_10m"].as<int>();
  out.weather_code = current["weather_code"].as<int>();

  // Try to get precipitation probability 3 hours from now
  JsonArray probArr = doc["hourly"]["precipitation_probability"];
  if (!probArr.isNull() && probArr.size() >= 4) {
    out.precipitation_probability_3h = probArr[3].as<int>();
  } else {
    out.precipitation_probability_3h = -1;
  }

  return true;
}

bool WeatherClient::parseHistoricalPrecipitation(const char* json, bool& rained,
                                                  uint8_t startHour,
                                                  uint8_t durationHours) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;

  JsonArray precip = doc["hourly"]["precipitation"];
  if (precip.isNull()) return false;

  size_t from = startHour;
  size_t to = startHour + durationHours;  // exclusive
  if (to > precip.size()) to = precip.size();

  rained = false;
  for (size_t i = from; i < to; i++) {
    if (precip[i].as<float>() >= RAIN_THRESHOLD_MM) {
      rained = true;
      break;
    }
  }
  return true;
}

const char* WeatherClient::windDirectionToCompass(int degrees) {
  // Normalize to 0-359
  degrees = ((degrees % 360) + 360) % 360;

  // 8 compass directions, each sector is 45 degrees
  // Add 22 to shift so that 0 (N) is centered on the sector
  int index = ((degrees + 22) / 45) % 8;

  static const char* directions[] = {"N", "NE", "E", "SE",
                                      "S", "SW", "W", "NW"};
  return directions[index];
}
