#include "strava_client.h"

#include <ArduinoJson.h>
#include <cstring>

static constexpr float METERS_PER_KM = 1000.0f;

bool StravaClient::parseTokenResponse(const char* json, char* accessToken,
                                       size_t atLen, char* refreshToken,
                                       size_t rtLen,
                                       unsigned long& expiresAt) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;

  const char* at = doc["access_token"];
  const char* rt = doc["refresh_token"];
  if (!at || !rt || doc["expires_at"].isNull()) return false;

  strncpy(accessToken, at, atLen - 1);
  accessToken[atLen - 1] = '\0';
  strncpy(refreshToken, rt, rtLen - 1);
  refreshToken[rtLen - 1] = '\0';
  expiresAt = doc["expires_at"].as<unsigned long>();

  return true;
}

bool StravaClient::parseStats(const char* json, StravaStats& out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;

  JsonObject allRide = doc["all_ride_totals"];
  if (allRide.isNull()) return false;

  out.all_ride_totals_km = allRide["distance"].as<float>() / METERS_PER_KM;
  out.all_ride_count = allRide["count"].as<int>();

  JsonObject ytdRide = doc["ytd_ride_totals"];
  if (!ytdRide.isNull()) {
    out.ytd_ride_totals_km = ytdRide["distance"].as<float>() / METERS_PER_KM;
  } else {
    out.ytd_ride_totals_km = 0.0f;
  }

  JsonObject recentRide = doc["recent_ride_totals"];
  if (!recentRide.isNull()) {
    out.recent_ride_weekly_avg_km =
        recentRide["distance"].as<float>() / 4.0f / METERS_PER_KM;
  } else {
    out.recent_ride_weekly_avg_km = 0.0f;
  }

  return true;
}

bool StravaClient::parseActivitiesDistance(const char* json,
                                            float& totalDistanceKm) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;

  JsonArray arr = doc.as<JsonArray>();
  float totalMeters = 0.0f;
  for (JsonObject activity : arr) {
    totalMeters += activity["distance"].as<float>();
  }
  totalDistanceKm = totalMeters / METERS_PER_KM;
  return true;
}

bool StravaClient::parseActivitiesStats(const char* json,
                                         StravaActivitiesStats& out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;

  JsonArray arr = doc.as<JsonArray>();
  float totalMeters = 0.0f;
  float totalElevation = 0.0f;
  for (JsonObject activity : arr) {
    totalMeters += activity["distance"].as<float>();
    totalElevation += activity["total_elevation_gain"].as<float>();
  }
  out.total_distance_km = totalMeters / METERS_PER_KM;
  out.total_elevation_m = totalElevation;
  return true;
}

bool StravaClient::parseActivity(const char* json, StravaActivity& out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;

  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() == 0) return false;

  JsonObject activity = arr[0];
  const char* name = activity["name"];
  if (name) {
    strncpy(out.name, name, sizeof(out.name) - 1);
    out.name[sizeof(out.name) - 1] = '\0';
  } else {
    out.name[0] = '\0';
  }

  out.distance_km = activity["distance"].as<float>() / METERS_PER_KM;
  out.moving_time_sec = activity["moving_time"].as<uint32_t>();

  return true;
}
