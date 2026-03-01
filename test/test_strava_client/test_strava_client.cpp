#include <unity.h>
#include <cstring>

#include "strava_client.h"

// --- parseTokenResponse tests ---

void test_parse_token_response_success(void) {
  const char* json = R"({
    "token_type": "Bearer",
    "expires_at": 1709280000,
    "expires_in": 21600,
    "refresh_token": "new_refresh_token_abc",
    "access_token": "new_access_token_xyz"
  })";

  char accessToken[128];
  char refreshToken[128];
  unsigned long expiresAt;

  bool ok = StravaClient::parseTokenResponse(json, accessToken,
                                              sizeof(accessToken),
                                              refreshToken,
                                              sizeof(refreshToken), expiresAt);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("new_access_token_xyz", accessToken);
  TEST_ASSERT_EQUAL_STRING("new_refresh_token_abc", refreshToken);
  TEST_ASSERT_EQUAL_UINT32(1709280000, expiresAt);
}

void test_parse_token_response_invalid_json(void) {
  const char* json = "not valid json";

  char accessToken[128];
  char refreshToken[128];
  unsigned long expiresAt;

  bool ok = StravaClient::parseTokenResponse(json, accessToken,
                                              sizeof(accessToken),
                                              refreshToken,
                                              sizeof(refreshToken), expiresAt);
  TEST_ASSERT_FALSE(ok);
}

void test_parse_token_response_missing_fields(void) {
  const char* json = R"({"token_type": "Bearer"})";

  char accessToken[128];
  char refreshToken[128];
  unsigned long expiresAt;

  bool ok = StravaClient::parseTokenResponse(json, accessToken,
                                              sizeof(accessToken),
                                              refreshToken,
                                              sizeof(refreshToken), expiresAt);
  TEST_ASSERT_FALSE(ok);
}

// --- parseStats tests ---

void test_parse_stats_success(void) {
  const char* json = R"({
    "all_ride_totals": {
      "count": 150,
      "distance": 12345678.9,
      "moving_time": 500000,
      "elapsed_time": 600000,
      "elevation_gain": 50000.0
    },
    "ytd_ride_totals": {
      "count": 20,
      "distance": 1234567.8,
      "moving_time": 50000,
      "elapsed_time": 60000,
      "elevation_gain": 5000.0
    }
  })";

  StravaStats stats;
  bool ok = StravaClient::parseStats(json, stats);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 12345.7f, stats.all_ride_totals_km);
  TEST_ASSERT_EQUAL_INT(150, stats.all_ride_count);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1234.6f, stats.ytd_ride_totals_km);
}

void test_parse_stats_invalid_json(void) {
  const char* json = "garbage";

  StravaStats stats;
  bool ok = StravaClient::parseStats(json, stats);
  TEST_ASSERT_FALSE(ok);
}

void test_parse_stats_missing_ride_totals(void) {
  const char* json = R"({"recent_run_totals": {}})";

  StravaStats stats;
  bool ok = StravaClient::parseStats(json, stats);
  TEST_ASSERT_FALSE(ok);
}

// --- parseActivity tests ---

void test_parse_activity_success(void) {
  const char* json = R"([{
    "name": "Morning Ride",
    "distance": 42195.0,
    "moving_time": 5400,
    "start_date_local": "2026-03-01T08:00:00Z",
    "type": "Ride"
  }])";

  StravaActivity activity;
  bool ok = StravaClient::parseActivity(json, activity);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("Morning Ride", activity.name);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 42.2f, activity.distance_km);
  TEST_ASSERT_EQUAL_UINT32(5400, activity.moving_time_sec);
}

void test_parse_activity_empty_array(void) {
  const char* json = "[]";

  StravaActivity activity;
  bool ok = StravaClient::parseActivity(json, activity);
  TEST_ASSERT_FALSE(ok);
}

void test_parse_activity_long_name_truncated(void) {
  const char* json = R"([{
    "name": "This is a very long activity name that should be truncated to fit the buffer",
    "distance": 10000.0,
    "moving_time": 1800,
    "start_date_local": "2026-03-01T08:00:00Z",
    "type": "Ride"
  }])";

  StravaActivity activity;
  bool ok = StravaClient::parseActivity(json, activity);
  TEST_ASSERT_TRUE(ok);
  // Name should be truncated but not overflow
  TEST_ASSERT_TRUE(strlen(activity.name) < sizeof(activity.name));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_token_response_success);
  RUN_TEST(test_parse_token_response_invalid_json);
  RUN_TEST(test_parse_token_response_missing_fields);
  RUN_TEST(test_parse_stats_success);
  RUN_TEST(test_parse_stats_invalid_json);
  RUN_TEST(test_parse_stats_missing_ride_totals);
  RUN_TEST(test_parse_activity_success);
  RUN_TEST(test_parse_activity_empty_array);
  RUN_TEST(test_parse_activity_long_name_truncated);
  UNITY_END();
  return 0;
}
