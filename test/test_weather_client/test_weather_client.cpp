#include <unity.h>
#include <cstring>

#include "weather_client.h"

// --- parseWeather tests ---

void test_parse_weather_full_response(void) {
  const char* json = R"({
    "current": {
      "wind_speed_10m": 12.5,
      "wind_direction_10m": 225,
      "weather_code": 3
    },
    "hourly": {
      "precipitation_probability": [10, 15, 20, 30]
    }
  })";

  WeatherData data;
  bool ok = WeatherClient::parseWeather(json, data);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 12.5f, data.wind_speed_kmh);
  TEST_ASSERT_EQUAL_INT(225, data.wind_direction_deg);
  TEST_ASSERT_EQUAL_INT(3, data.weather_code);
  TEST_ASSERT_EQUAL_INT(30, data.precipitation_probability_3h);
}

void test_parse_weather_no_hourly(void) {
  const char* json = R"({
    "current": {
      "wind_speed_10m": 5.0,
      "wind_direction_10m": 90,
      "weather_code": 0
    }
  })";

  WeatherData data;
  bool ok = WeatherClient::parseWeather(json, data);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 5.0f, data.wind_speed_kmh);
  TEST_ASSERT_EQUAL_INT(90, data.wind_direction_deg);
  TEST_ASSERT_EQUAL_INT(0, data.weather_code);
  TEST_ASSERT_EQUAL_INT(-1, data.precipitation_probability_3h);
}

void test_parse_weather_hourly_too_short(void) {
  const char* json = R"({
    "current": {
      "wind_speed_10m": 8.0,
      "wind_direction_10m": 180,
      "weather_code": 1
    },
    "hourly": {
      "precipitation_probability": [10, 20]
    }
  })";

  WeatherData data;
  bool ok = WeatherClient::parseWeather(json, data);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_INT(-1, data.precipitation_probability_3h);
}

void test_parse_weather_no_current(void) {
  const char* json = R"({
    "hourly": {
      "precipitation_probability": [10, 15, 20, 30]
    }
  })";

  WeatherData data;
  bool ok = WeatherClient::parseWeather(json, data);
  TEST_ASSERT_FALSE(ok);
}

void test_parse_weather_invalid_json(void) {
  const char* json = "not valid json";

  WeatherData data;
  bool ok = WeatherClient::parseWeather(json, data);
  TEST_ASSERT_FALSE(ok);
}

// --- windDirectionToCompass tests ---

void test_compass_north(void) {
  TEST_ASSERT_EQUAL_STRING("N", WeatherClient::windDirectionToCompass(0));
  TEST_ASSERT_EQUAL_STRING("N", WeatherClient::windDirectionToCompass(22));
  TEST_ASSERT_EQUAL_STRING("N", WeatherClient::windDirectionToCompass(338));
  TEST_ASSERT_EQUAL_STRING("N", WeatherClient::windDirectionToCompass(360));
}

void test_compass_cardinal(void) {
  TEST_ASSERT_EQUAL_STRING("E", WeatherClient::windDirectionToCompass(90));
  TEST_ASSERT_EQUAL_STRING("S", WeatherClient::windDirectionToCompass(180));
  TEST_ASSERT_EQUAL_STRING("W", WeatherClient::windDirectionToCompass(270));
}

void test_compass_intercardinal(void) {
  TEST_ASSERT_EQUAL_STRING("NE", WeatherClient::windDirectionToCompass(45));
  TEST_ASSERT_EQUAL_STRING("SE", WeatherClient::windDirectionToCompass(135));
  TEST_ASSERT_EQUAL_STRING("SW", WeatherClient::windDirectionToCompass(225));
  TEST_ASSERT_EQUAL_STRING("NW", WeatherClient::windDirectionToCompass(315));
}

void test_compass_boundary(void) {
  // 8 sectors of 45 degrees each, centered on cardinal/intercardinal
  // N: 338-22, NE: 23-67, etc. (integer math: (deg+22)/45)
  TEST_ASSERT_EQUAL_STRING("NE", WeatherClient::windDirectionToCompass(23));
  TEST_ASSERT_EQUAL_STRING("NW", WeatherClient::windDirectionToCompass(337));
  TEST_ASSERT_EQUAL_STRING("N", WeatherClient::windDirectionToCompass(338));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_weather_full_response);
  RUN_TEST(test_parse_weather_no_hourly);
  RUN_TEST(test_parse_weather_hourly_too_short);
  RUN_TEST(test_parse_weather_no_current);
  RUN_TEST(test_parse_weather_invalid_json);
  RUN_TEST(test_compass_north);
  RUN_TEST(test_compass_cardinal);
  RUN_TEST(test_compass_intercardinal);
  RUN_TEST(test_compass_boundary);
  UNITY_END();
  return 0;
}
