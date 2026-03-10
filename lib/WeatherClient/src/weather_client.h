#ifndef WEATHER_CLIENT_H
#define WEATHER_CLIENT_H

#include <stdint.h>

struct WeatherData {
  float wind_speed_kmh;
  int wind_direction_deg;
  int weather_code;
  int precipitation_probability_3h;  // -1 if unavailable
};

class WeatherClient {
public:
  static bool parseWeather(const char* json, WeatherData& out);
  static bool parseHistoricalPrecipitation(const char* json, bool& rained,
                                           uint8_t startHour = 0,
                                           uint8_t durationHours = 24);
  static const char* windDirectionToCompass(int degrees);
  static constexpr float RAIN_THRESHOLD_MM = 0.5f;
};

#endif
