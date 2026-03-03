#ifndef STRAVA_CLIENT_H
#define STRAVA_CLIENT_H

#include <stdint.h>
#include <ctime>

struct StravaActivity {
  char name[48];
  float distance_km;
  uint32_t moving_time_sec;
  char type[24];        // "Ride", "VirtualRide", "Run" etc.
  char start_date[11];  // "YYYY-MM-DD" (10 chars + null)
  float start_lat;
  float start_lng;
  bool has_location;    // true if start_latlng was non-null/non-empty
};

struct StravaActivitiesStats {
  float total_distance_km;
  float total_elevation_m;
};

struct StravaStats {
  float all_ride_totals_km;
  int all_ride_count;
  float ytd_ride_totals_km;
  float recent_ride_weekly_avg_km;  // recent_ride_totals / 4 weeks
};

class StravaClient {
public:
  static bool parseActivity(const char* json, StravaActivity& out);
  static bool parseActivitiesDistance(const char* json, float& totalDistanceKm);
  static bool parseActivitiesStats(const char* json, StravaActivitiesStats& out);
  static bool parseStats(const char* json, StravaStats& out);
  static bool parseTokenResponse(const char* json, char* accessToken,
                                  size_t atLen, char* refreshToken,
                                  size_t rtLen, unsigned long& expiresAt);
};

#endif
