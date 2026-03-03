#include <M5Stack.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <time.h>

#include "QMP6988.h"
#include "SHT3X.h"
#include "maintenance_display.h"
#include "maintenance_tracker.h"
#include "pressure_trend.h"
#include "rain_ride_detector.h"
#include "strava_client.h"
#include "strava_config.h"
#include "weather_client.h"
#include "weather_config.h"
#include "wifi_config.h"

static const uint8_t ENV_SDA = 21;
static const uint8_t ENV_SCL = 22;
static const unsigned long READ_INTERVAL_MS = 10000;
static const unsigned long NVS_SAVE_INTERVAL_MS = 60000;
static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 30000;
static const unsigned long NTP_RESYNC_INTERVAL_MS = 3600000;  // 1 hour
static const unsigned long NTP_CHECK_INTERVAL_MS = 5000;
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;
static const unsigned long STRAVA_SYNC_INTERVAL_MS = 600000;    // 10 min
static const unsigned long STRAVA_TOKEN_EXPIRY_BUFFER_SEC = 300;  // 5 min
static const unsigned long WEATHER_SYNC_INTERVAL_MS = 1800000;  // 30 min
static const long GMT_OFFSET_SEC = 9 * 3600;                   // JST

// --- Color Palette (Dracula) ---
// https://draculatheme.com/contribute
static const uint16_t COL_BG             = 0x2946;  // #282a36 Background
static const uint16_t COL_HEADER_BG      = 0x422B;  // #44475a Current Line
static const uint16_t COL_DIVIDER        = 0x422B;  // #44475a Current Line
static const uint16_t COL_TEXT_PRIMARY    = 0xFFDE;  // #f8f8f2 Foreground
static const uint16_t COL_TEXT_SECONDARY  = 0x8CF8;  // #8b9dc7 Light Comment
static const uint16_t COL_TEXT_MUTED      = 0x6394;  // #6272a4 Comment
static const uint16_t COL_ACCENT_CYAN    = 0x8F5F;  // #8be9fd Cyan
static const uint16_t COL_ACCENT_BLUE    = 0xBC9F;  // #bd93f9 Purple
static const uint16_t COL_ACCENT_GREEN   = 0x57CF;  // #50fa7b Green
static const uint16_t COL_ACCENT_AMBER   = 0xFDCD;  // #ffb86c Orange
static const uint16_t COL_ACCENT_RED     = 0xFAAA;  // #ff5555 Red
static const uint16_t COL_WARN_YELLOW    = 0xF7F1;  // #f1fa8c Yellow
static const uint16_t COL_CRIT_RED       = 0xFAAA;  // #ff5555 Red

SHT3X sht3x;
QMP6988 qmp6988;
PressureTrend pressureTrend;
Preferences preferences;

MaintenanceTracker tirePressure;
MaintenanceTracker chainLube;

bool sensorOk = false;
unsigned long lastReadMs = 0;
unsigned long lastNvsSaveMs = 0;
unsigned long lastWifiCheckMs = 0;
unsigned long lastNtpSyncMs = 0;
unsigned long lastNtpCheckMs = 0;
unsigned long lastStravaSyncMs = 0;
float temperature = 0.0f;
float humidity = 0.0f;
float pressure_hpa = 0.0f;

// Cumulative uptime persisted across reboots
uint64_t cumulativeUptimeMs = 0;
unsigned long lastUptimeUpdateMs = 0;

bool ntpSynced = false;

// Strava state
char stravaAccessToken[256] = "";
char stravaRefreshToken[256] = "";
unsigned long stravaExpiresAt = 0;
StravaStats stravaStats = {};
StravaActivity stravaLatestActivity = {"", 0.0f, 0, "", "", 0.0f, 0.0f, false};
bool stravaDataValid = false;

// Weekly/Monthly stats
float weeklyDistanceKm = 0.0f;
float weeklyAverageKm = 0.0f;
float monthlyDistanceKm = 0.0f;
float monthlyElevationM = 0.0f;
bool stravaSyncNeeded = true;
unsigned long stravaBackoffUntilMs = 0;
static const unsigned long STRAVA_BACKOFF_MS = 900000;  // 15 min after 429

// Chain lube distance tracking
float chainLubeDistanceKm = 0.0f;
bool chainLubeDistanceValid = false;

// Weather state
WeatherData weatherData = {};
bool weatherDataValid = false;
unsigned long lastWeatherSyncMs = 0;
bool weatherSyncNeeded = true;

// Rain ride detection
bool rainRideFlag = false;

static uint64_t currentCumulativeMs() {
  return cumulativeUptimeMs + (millis() - lastUptimeUpdateMs);
}

static const char* trendSymbol(TrendDirection dir) {
  switch (dir) {
    case TrendDirection::TREND_RISING:
      return "^";
    case TrendDirection::TREND_FALLING:
      return "v";
    default:
      return "-";
  }
}

// --- Icon Drawing Helpers ---

void drawThermometer(int x, int y, uint16_t color) {
  M5.Lcd.fillRect(x + 3, y, 4, 12, color);
  M5.Lcd.fillCircle(x + 5, y + 14, 4, color);
}

void drawDrop(int x, int y, uint16_t color) {
  M5.Lcd.fillTriangle(x + 5, y, x + 1, y + 8, x + 9, y + 8, color);
  M5.Lcd.fillCircle(x + 5, y + 10, 5, color);
}

void drawTrendArrow(int x, int y, TrendDirection dir, uint16_t color) {
  M5.Lcd.fillRect(x, y, 10, 14, COL_BG);
  if (dir == TrendDirection::TREND_RISING) {
    M5.Lcd.fillTriangle(x + 4, y, x, y + 6, x + 8, y + 6, color);
    M5.Lcd.fillRect(x + 2, y + 6, 4, 4, color);
  } else if (dir == TrendDirection::TREND_FALLING) {
    M5.Lcd.fillRect(x + 2, y, 4, 4, color);
    M5.Lcd.fillTriangle(x + 4, y + 10, x, y + 4, x + 8, y + 4, color);
  } else {
    M5.Lcd.fillRect(x, y + 4, 8, 3, color);
  }
}

void drawWeatherIcon(int x, int y, int code, uint16_t color) {
  M5.Lcd.fillRect(x, y, 20, 16, COL_BG);
  if (code == 0) {
    // Sun
    M5.Lcd.fillCircle(x + 10, y + 8, 4, color);
    M5.Lcd.drawLine(x + 10, y, x + 10, y + 2, color);
    M5.Lcd.drawLine(x + 10, y + 14, x + 10, y + 16, color);
    M5.Lcd.drawLine(x + 2, y + 8, x + 4, y + 8, color);
    M5.Lcd.drawLine(x + 16, y + 8, x + 18, y + 8, color);
    M5.Lcd.drawLine(x + 4, y + 2, x + 6, y + 4, color);
    M5.Lcd.drawLine(x + 16, y + 2, x + 14, y + 4, color);
    M5.Lcd.drawLine(x + 4, y + 14, x + 6, y + 12, color);
    M5.Lcd.drawLine(x + 16, y + 14, x + 14, y + 12, color);
  } else if (code <= 3) {
    // Cloud
    M5.Lcd.fillCircle(x + 6, y + 10, 4, color);
    M5.Lcd.fillCircle(x + 14, y + 10, 4, color);
    M5.Lcd.fillCircle(x + 10, y + 6, 5, color);
    M5.Lcd.fillRect(x + 6, y + 10, 8, 4, color);
  } else if (code <= 67) {
    // Rain / drizzle / fog
    M5.Lcd.fillCircle(x + 5, y + 4, 3, color);
    M5.Lcd.fillCircle(x + 12, y + 4, 3, color);
    M5.Lcd.fillCircle(x + 9, y + 2, 3, color);
    M5.Lcd.fillRect(x + 5, y + 4, 7, 3, color);
    M5.Lcd.drawLine(x + 5, y + 9, x + 5, y + 13, color);
    M5.Lcd.drawLine(x + 9, y + 9, x + 9, y + 13, color);
    M5.Lcd.drawLine(x + 13, y + 9, x + 13, y + 13, color);
  } else if (code <= 77) {
    // Snow
    M5.Lcd.fillCircle(x + 5, y + 4, 3, color);
    M5.Lcd.fillCircle(x + 12, y + 4, 3, color);
    M5.Lcd.fillCircle(x + 9, y + 2, 3, color);
    M5.Lcd.fillRect(x + 5, y + 4, 7, 3, color);
    M5.Lcd.fillCircle(x + 5, y + 11, 1, color);
    M5.Lcd.fillCircle(x + 9, y + 13, 1, color);
    M5.Lcd.fillCircle(x + 13, y + 11, 1, color);
  } else {
    // Thunderstorm / heavy rain
    M5.Lcd.fillCircle(x + 5, y + 4, 3, color);
    M5.Lcd.fillCircle(x + 12, y + 4, 3, color);
    M5.Lcd.fillCircle(x + 9, y + 2, 3, color);
    M5.Lcd.fillRect(x + 5, y + 4, 7, 3, color);
    M5.Lcd.drawLine(x + 5, y + 9, x + 5, y + 14, color);
    M5.Lcd.drawLine(x + 9, y + 9, x + 9, y + 14, color);
    M5.Lcd.drawLine(x + 13, y + 9, x + 13, y + 14, color);
  }
}

void drawWifiIcon(int x, int y, uint16_t color) {
  int cx = x + 8;
  int cy = y + 11;
  M5.Lcd.fillRect(x, y, 16, 13, COL_BG);
  M5.Lcd.drawCircleHelper(cx, cy, 10, 0x1 | 0x2, color);
  M5.Lcd.drawCircleHelper(cx, cy, 7, 0x1 | 0x2, color);
  M5.Lcd.drawCircleHelper(cx, cy, 4, 0x1 | 0x2, color);
  M5.Lcd.fillCircle(cx, cy, 1, color);
}

void drawBicycle(int x, int y, uint16_t color) {
  M5.Lcd.fillRect(x, y, 20, 16, COL_BG);
  M5.Lcd.drawCircle(x + 4, y + 11, 4, color);
  M5.Lcd.drawCircle(x + 16, y + 11, 4, color);
  M5.Lcd.drawLine(x + 4, y + 11, x + 10, y + 5, color);
  M5.Lcd.drawLine(x + 10, y + 5, x + 16, y + 11, color);
  M5.Lcd.drawLine(x + 4, y + 11, x + 12, y + 11, color);
  M5.Lcd.drawLine(x + 10, y + 5, x + 7, y + 3, color);
}

void drawTireIcon(int x, int y, uint16_t color) {
  M5.Lcd.fillRect(x, y, 36, 36, COL_BG);
  int cx = x + 18, cy = y + 18;
  // Outer tire (double line)
  M5.Lcd.drawCircle(cx, cy, 17, color);
  M5.Lcd.drawCircle(cx, cy, 16, color);
  // Inner rim
  M5.Lcd.drawCircle(cx, cy, 12, color);
  // Hub
  M5.Lcd.fillCircle(cx, cy, 3, color);
  // 8 spokes (cardinal + diagonal)
  M5.Lcd.drawLine(cx, cy - 4, cx, cy - 12, color);
  M5.Lcd.drawLine(cx, cy + 4, cx, cy + 12, color);
  M5.Lcd.drawLine(cx - 4, cy, cx - 12, cy, color);
  M5.Lcd.drawLine(cx + 4, cy, cx + 12, cy, color);
  M5.Lcd.drawLine(cx + 2, cy - 2, cx + 9, cy - 9, color);
  M5.Lcd.drawLine(cx - 2, cy - 2, cx - 9, cy - 9, color);
  M5.Lcd.drawLine(cx + 2, cy + 2, cx + 9, cy + 9, color);
  M5.Lcd.drawLine(cx - 2, cy + 2, cx - 9, cy + 9, color);
}

void drawChainIcon(int x, int y, uint16_t color) {
  M5.Lcd.fillRect(x, y, 72, 36, COL_BG);
  // 5 interlocking chain links (horizontal rounded rects)
  for (int i = 0; i < 5; i++) {
    int lx = x + 1 + i * 13;
    int ly = y + 11;
    M5.Lcd.drawRoundRect(lx, ly, 20, 14, 6, color);
    M5.Lcd.drawRoundRect(lx + 1, ly + 1, 18, 12, 5, color);
  }
}

void initWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("Connecting to WiFi: %s", WIFI_SSID);

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - startMs) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi connected: %s\n",
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi connection failed (will retry in loop)");
  }
}

void initNtp() {
  configTime(GMT_OFFSET_SEC, 0, "ntp.nict.jp", "pool.ntp.org");
  Serial.println("NTP configured (JST, ntp.nict.jp)");
}

bool checkNtpSync() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 100)) {
    if (timeinfo.tm_year > (2020 - 1900)) {
      return true;
    }
  }
  return false;
}

void updateResetEpoch(MaintenanceTracker& tracker) {
  if (ntpSynced) {
    time_t epochNow;
    time(&epochNow);
    tracker.setResetEpoch(epochNow);
  }
}

// --- Strava token management ---

void saveStravaTokens() {
  preferences.putString("strava_at", stravaAccessToken);
  preferences.putString("strava_rt", stravaRefreshToken);
  preferences.putULong64("strava_exp", static_cast<uint64_t>(stravaExpiresAt));
  Serial.println("Strava tokens saved to NVS");
}

void loadStravaTokens() {
  String at = preferences.getString("strava_at", "");
  String rt = preferences.getString("strava_rt", "");
  stravaExpiresAt = static_cast<unsigned long>(
      preferences.getULong64("strava_exp", 0));

  strncpy(stravaAccessToken, at.c_str(), sizeof(stravaAccessToken) - 1);
  stravaAccessToken[sizeof(stravaAccessToken) - 1] = '\0';
  strncpy(stravaRefreshToken, rt.c_str(), sizeof(stravaRefreshToken) - 1);
  stravaRefreshToken[sizeof(stravaRefreshToken) - 1] = '\0';

  // If no stored refresh token, use the one from config
  if (strlen(stravaRefreshToken) == 0) {
    strncpy(stravaRefreshToken, STRAVA_REFRESH_TOKEN,
            sizeof(stravaRefreshToken) - 1);
    stravaRefreshToken[sizeof(stravaRefreshToken) - 1] = '\0';
  }

  Serial.printf("Strava tokens loaded: AT=%s RT=%s\n",
                strlen(stravaAccessToken) > 0 ? "(set)" : "(empty)",
                strlen(stravaRefreshToken) > 0 ? "(set)" : "(empty)");
}

bool refreshStravaToken() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, "https://www.strava.com/oauth/token")) {
    Serial.println("Strava: Failed to begin HTTP");
    return false;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  char payload[512];
  snprintf(payload, sizeof(payload),
           "client_id=%s&client_secret=%s&grant_type=refresh_token&refresh_token=%s",
           STRAVA_CLIENT_ID, STRAVA_CLIENT_SECRET, stravaRefreshToken);

  int httpCode = http.POST(payload);
  Serial.printf("Strava token refresh: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    char newAt[256];
    char newRt[256];
    unsigned long newExp;

    if (StravaClient::parseTokenResponse(response.c_str(), newAt, sizeof(newAt),
                                          newRt, sizeof(newRt), newExp)) {
      strncpy(stravaAccessToken, newAt, sizeof(stravaAccessToken) - 1);
      strncpy(stravaRefreshToken, newRt, sizeof(stravaRefreshToken) - 1);
      stravaExpiresAt = newExp;
      saveStravaTokens();
      Serial.println("Strava token refreshed successfully");
      http.end();
      return true;
    }
  }

  http.end();
  Serial.println("Strava token refresh failed");
  return false;
}

bool ensureStravaToken() {
  if (strlen(stravaAccessToken) == 0 || strlen(stravaRefreshToken) == 0) {
    // No tokens at all, try refresh with config token
    return refreshStravaToken();
  }

  // Check if token is expired (with 5 min buffer)
  if (ntpSynced) {
    time_t now;
    time(&now);
    if (static_cast<unsigned long>(now) >= stravaExpiresAt - STRAVA_TOKEN_EXPIRY_BUFFER_SEC) {
      return refreshStravaToken();
    }
  } else {
    // Without NTP, refresh if access token seems stale
    if (stravaExpiresAt == 0) {
      return refreshStravaToken();
    }
  }

  return true;
}

// --- Strava API calls ---

bool fetchStravaStats(bool isRetry = false) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://www.strava.com/api/v3/athletes/" +
               String(STRAVA_ATHLETE_ID) + "/stats";

  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader("Authorization", "Bearer " + String(stravaAccessToken));

  int httpCode = http.GET();
  Serial.printf("Strava stats: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    bool ok = StravaClient::parseStats(response.c_str(), stravaStats);
    http.end();
    if (ok) {
      Serial.printf("Strava stats: %.1f km total, %d rides\n",
                    stravaStats.all_ride_totals_km, stravaStats.all_ride_count);
    }
    return ok;
  }

  if (httpCode == 401 && !isRetry) {
    Serial.println("Strava: Unauthorized, will refresh token");
    http.end();
    if (refreshStravaToken()) {
      return fetchStravaStats(true);  // Retry once after refresh
    }
    return false;
  }

  http.end();
  return false;
}

bool fetchStravaLatestActivity() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client,
                  "https://www.strava.com/api/v3/athlete/activities?per_page=1")) {
    return false;
  }

  http.addHeader("Authorization", "Bearer " + String(stravaAccessToken));

  int httpCode = http.GET();
  Serial.printf("Strava activities: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    bool ok =
        StravaClient::parseActivity(response.c_str(), stravaLatestActivity);
    http.end();
    if (ok) {
      Serial.printf("Strava latest: %s (%.1f km)\n",
                    stravaLatestActivity.name,
                    stravaLatestActivity.distance_km);
    }
    return ok;
  }

  http.end();
  return false;
}

bool fetchChainLubeDistance(bool isRetry = false) {
  time_t resetEpoch = chainLube.resetEpoch();
  if (resetEpoch == 0) {
    // No reset epoch recorded; cannot query by date
    chainLubeDistanceValid = false;
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://www.strava.com/api/v3/athlete/activities?after=" +
               String(static_cast<unsigned long>(resetEpoch)) + "&per_page=200";

  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader("Authorization", "Bearer " + String(stravaAccessToken));

  int httpCode = http.GET();
  Serial.printf("Strava chain lube distance: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    float totalKm = 0.0f;
    bool ok = StravaClient::parseActivitiesDistance(response.c_str(), totalKm);
    http.end();
    if (ok) {
      chainLubeDistanceKm = totalKm;
      chainLubeDistanceValid = true;
      Serial.printf("Chain lube distance since reset: %.1f km\n", totalKm);
    }
    return ok;
  }

  if (httpCode == 401 && !isRetry) {
    Serial.println("Strava: Unauthorized, will refresh token");
    http.end();
    if (refreshStravaToken()) {
      return fetchChainLubeDistance(true);
    }
    return false;
  }

  http.end();
  return false;
}

bool fetchWeeklyDistance() {
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  // Strava week starts on Monday (tm_wday: 0=Sun, 1=Mon, ..., 6=Sat)
  int daysSinceMonday = (timeinfo.tm_wday + 6) % 7;
  timeinfo.tm_mday -= daysSinceMonday;
  timeinfo.tm_hour = 0;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;
  time_t weekStart = mktime(&timeinfo);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://www.strava.com/api/v3/athlete/activities?after=" +
               String(static_cast<unsigned long>(weekStart)) + "&per_page=200";

  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader("Authorization", "Bearer " + String(stravaAccessToken));

  int httpCode = http.GET();
  Serial.printf("Strava weekly: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    StravaActivitiesStats stats;
    bool ok = StravaClient::parseActivitiesStats(response.c_str(), stats);
    http.end();
    if (ok) {
      weeklyDistanceKm = stats.total_distance_km;
      Serial.printf("Weekly distance: %.1f km\n", weeklyDistanceKm);
    }
    return ok;
  }

  http.end();
  return false;
}

bool fetchMonthlyStats() {
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  timeinfo.tm_mday = 1;
  timeinfo.tm_hour = 0;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;
  time_t monthStart = mktime(&timeinfo);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://www.strava.com/api/v3/athlete/activities?after=" +
               String(static_cast<unsigned long>(monthStart)) + "&per_page=200";

  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader("Authorization", "Bearer " + String(stravaAccessToken));

  int httpCode = http.GET();
  Serial.printf("Strava monthly: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    StravaActivitiesStats stats;
    bool ok = StravaClient::parseActivitiesStats(response.c_str(), stats);
    http.end();
    if (ok) {
      monthlyDistanceKm = stats.total_distance_km;
      monthlyElevationM = stats.total_elevation_m;
      Serial.printf("Monthly: %.1f km, %.0f m elevation\n",
                    monthlyDistanceKm, monthlyElevationM);
    }
    return ok;
  }

  http.end();
  return false;
}

bool checkRainRide(const StravaActivity& activity) {
  if (!RainRideDetector::isOutdoorRide(activity.type)) return false;
  if (strlen(activity.start_date) == 0) return false;

  float lat, lng;
  if (activity.has_location) {
    lat = activity.start_lat;
    lng = activity.start_lng;
  } else {
    // Fall back to configured weather location
    lat = atof(WEATHER_LAT);
    lng = atof(WEATHER_LON);
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  char url[256];
  snprintf(url, sizeof(url),
           "https://archive-api.open-meteo.com/v1/archive"
           "?latitude=%.4f&longitude=%.4f&start_date=%s&end_date=%s"
           "&hourly=precipitation&timezone=Asia%%2FTokyo",
           lat, lng, activity.start_date, activity.start_date);

  if (!http.begin(client, url)) {
    Serial.println("Rain check: Failed to begin HTTP");
    return false;
  }

  int httpCode = http.GET();
  Serial.printf("Rain check: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    bool rained = false;
    if (WeatherClient::parseHistoricalPrecipitation(response.c_str(), rained)) {
      Serial.printf("Rain check: rained=%s\n", rained ? "YES" : "NO");
      http.end();
      return rained;
    }
  }

  http.end();
  return false;
}

void syncStrava() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Skip if in backoff period after HTTP 429
  if (stravaBackoffUntilMs != 0 && (long)(stravaBackoffUntilMs - millis()) > 0) {
    Serial.println("Strava sync skipped (rate limit backoff)");
    return;
  }

  Serial.println("Strava sync starting...");

  if (!ensureStravaToken()) {
    Serial.println("Strava: No valid token, skipping sync");
    return;
  }

  bool statsOk = fetchStravaStats();
  bool activityOk = fetchStravaLatestActivity();
  bool chainDistOk = fetchChainLubeDistance();
  bool weeklyOk = fetchWeeklyDistance();
  bool monthlyOk = fetchMonthlyStats();

  // Check rain ride if latest activity fetched and chain has a reset epoch
  if (activityOk && chainLube.resetEpoch() > 0) {
    if (checkRainRide(stravaLatestActivity)) {
      rainRideFlag = true;
      preferences.putBool("rain_ride", true);
      Serial.println("Rain ride detected — chain lube severity overridden");
    }
  }

  if (statsOk) {
    weeklyAverageKm = stravaStats.recent_ride_weekly_avg_km;
  }

  // Back off on total failure to avoid burning rate limit quota
  if (!statsOk && !activityOk && !chainDistOk && !weeklyOk && !monthlyOk) {
    stravaBackoffUntilMs = millis() + STRAVA_BACKOFF_MS;
    Serial.println("Strava sync all failed, backing off 15 min");
  }

  if (statsOk || activityOk) {
    stravaDataValid = true;
  }

  // Cache weekly/monthly stats to NVS
  if (weeklyOk) {
    preferences.putFloat("weekly_dist", weeklyDistanceKm);
  }
  if (statsOk) {
    preferences.putFloat("weekly_avg", weeklyAverageKm);
  }
  if (monthlyOk) {
    preferences.putFloat("month_dist", monthlyDistanceKm);
    preferences.putFloat("month_elev", monthlyElevationM);
  }

  Serial.printf("Strava sync done: stats=%s activity=%s chainDist=%s weekly=%s monthly=%s\n",
                statsOk ? "OK" : "FAIL", activityOk ? "OK" : "FAIL",
                chainDistOk ? "OK" : "FAIL", weeklyOk ? "OK" : "FAIL",
                monthlyOk ? "OK" : "FAIL");
}

// --- Weather API ---

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("Weather fetch starting...");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast"
               "?latitude=" WEATHER_LAT
               "&longitude=" WEATHER_LON
               "&current=wind_speed_10m,wind_direction_10m,weather_code"
               "&hourly=precipitation_probability"
               "&forecast_hours=4"
               "&timezone=Asia/Tokyo";

  if (!http.begin(client, url)) {
    Serial.println("Weather: Failed to begin HTTP");
    return;
  }

  int httpCode = http.GET();
  Serial.printf("Weather: HTTP %d\n", httpCode);

  if (httpCode == 200) {
    String response = http.getString();
    if (WeatherClient::parseWeather(response.c_str(), weatherData)) {
      weatherDataValid = true;
      Serial.printf("Weather: %.0f km/h %s code=%d precip3h=%d%%\n",
                    weatherData.wind_speed_kmh,
                    WeatherClient::windDirectionToCompass(weatherData.wind_direction_deg),
                    weatherData.weather_code,
                    weatherData.precipitation_probability_3h);
    } else {
      Serial.println("Weather: Parse failed");
    }
  }

  http.end();
}

// --- Sensors ---

void readSensors() {
  if (sht3x.update()) {
    temperature = sht3x.cTemp;
    humidity = sht3x.humidity;
  } else {
    Serial.println("Error: Failed to read SHT3X");
  }

  if (qmp6988.update()) {
    pressure_hpa = qmp6988.pressure / 100.0f;
    pressureTrend.addSample(pressure_hpa);
  } else {
    Serial.println("Error: Failed to read QMP6988");
  }
}

// --- Display ---

void drawStaticLayout() {
  M5.Lcd.fillScreen(COL_BG);

  // Room header bar
  M5.Lcd.fillRoundRect(1, 1, 157, 16, 3, COL_HEADER_BG);
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COL_ACCENT_CYAN, COL_HEADER_BG);
  M5.Lcd.drawString("Room", 6, 0);

  // INFO header bar
  M5.Lcd.fillRoundRect(161, 1, 157, 16, 3, COL_HEADER_BG);
  M5.Lcd.setTextColor(COL_ACCENT_BLUE, COL_HEADER_BG);
  M5.Lcd.drawString("INFO", 166, 0);

  // MAINTENANCE header bar
  M5.Lcd.fillRoundRect(1, 121, 317, 16, 3, COL_HEADER_BG);
  M5.Lcd.setTextColor(COL_ACCENT_AMBER, COL_HEADER_BG);
  M5.Lcd.setTextDatum(TC_DATUM);
  M5.Lcd.drawString("Tire", 80, 121);
  M5.Lcd.drawString("Chain", 240, 121);
  M5.Lcd.setTextDatum(TL_DATUM);

  // Dividers
  M5.Lcd.drawFastVLine(160, 0, 120, COL_DIVIDER);
  M5.Lcd.drawFastHLine(0, 120, 320, COL_DIVIDER);
  M5.Lcd.drawFastVLine(160, 139, 101, COL_DIVIDER);

  // Room/Out divider
  M5.Lcd.drawFastHLine(4, 66, 152, COL_DIVIDER);

  // "Out" sub-header bar
  M5.Lcd.fillRoundRect(1, 68, 157, 14, 3, COL_HEADER_BG);
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COL_ACCENT_CYAN, COL_HEADER_BG);
  M5.Lcd.drawString("Out", 6, 68);

  // Static Room icons
  drawThermometer(2, 22, COL_ACCENT_CYAN);
  drawDrop(2, 50, COL_ACCENT_CYAN);

  // Button hints — Powerline style bar
  {
    int h = 12;
    int arrowW = 6;
    int by = 228;
    int startX = 112;   // left arrow tip
    int sepX = 208;     // separator position
    int endX = 304;     // right edge

    // Left arrow (triangle pointing left)
    M5.Lcd.fillTriangle(startX, by + h / 2,
                         startX + arrowW, by,
                         startX + arrowW, by + h - 1,
                         COL_HEADER_BG);
    // Main bar
    M5.Lcd.fillRect(startX + arrowW, by, endX - startX - arrowW, h,
                     COL_HEADER_BG);
    // Separator (right-pointing arrow cutout in BG color)
    M5.Lcd.fillTriangle(sepX, by,
                         sepX, by + h - 1,
                         sepX + arrowW, by + h / 2,
                         COL_BG);

    // Text
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_ACCENT_AMBER, COL_HEADER_BG);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.drawString("B Reset", (startX + arrowW + sepX) / 2, by + h / 2);
    M5.Lcd.drawString("C Reset", (sepX + arrowW + endX) / 2, by + h / 2);
    M5.Lcd.setTextDatum(TL_DATUM);
  }
}

void drawEnvPanel() {
  char buf[32];

  // --- Room section (sensor data) ---
  if (!sensorOk) {
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_CRIT_RED, COL_BG);
    M5.Lcd.setTextPadding(140);
    M5.Lcd.drawString("Sensor Failed!", 16, 34);
    M5.Lcd.setTextPadding(0);
  } else {
    // Temperature (hero) — Font4, PRIMARY
    M5.Lcd.setTextFont(4);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_TEXT_PRIMARY, COL_BG);
    M5.Lcd.setTextPadding(80);
    snprintf(buf, sizeof(buf), "%.1f", temperature);
    int tw = M5.Lcd.drawString(buf, 16, 20);
    // Degree circle + C
    M5.Lcd.fillCircle(tw + 19, 23, 2, COL_TEXT_SECONDARY);
    M5.Lcd.setTextFont(2);
    M5.Lcd.setTextColor(COL_TEXT_SECONDARY, COL_BG);
    M5.Lcd.setTextPadding(0);
    M5.Lcd.drawString("C", tw + 24, 22);

    // Humidity + Pressure compact line — Font2, SECONDARY
    M5.Lcd.fillRect(14, 48, 142, 16, COL_BG);
    M5.Lcd.setTextFont(2);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_TEXT_SECONDARY, COL_BG);
    snprintf(buf, sizeof(buf), "%.1f%%", humidity);
    int hw = M5.Lcd.drawString(buf, 16, 48);
    // Pressure value
    int px = 16 + hw + 4;
    snprintf(buf, sizeof(buf), "%.0f", pressure_hpa);
    int pw = M5.Lcd.drawString(buf, px, 48);
    // Trend arrow
    drawTrendArrow(px + pw + 1, 48, pressureTrend.direction(), COL_TEXT_SECONDARY);
    // hPa label
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(COL_TEXT_MUTED, COL_BG);
    M5.Lcd.drawString("hPa", px + pw + 13, 52);
  }

  // --- Out section (weather, independent of sensor) ---
  if (weatherDataValid) {
    drawWeatherIcon(4, 84, weatherData.weather_code, COL_ACCENT_CYAN);
    // Wind speed + direction — Font2, CYAN
    M5.Lcd.setTextFont(2);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_ACCENT_CYAN, COL_BG);
    M5.Lcd.setTextPadding(126);
    snprintf(buf, sizeof(buf), "%.0fkm/h %s",
             weatherData.wind_speed_kmh,
             WeatherClient::windDirectionToCompass(weatherData.wind_direction_deg));
    M5.Lcd.drawString(buf, 26, 84);
    // Precipitation probability — Font1, CYAN
    if (weatherData.precipitation_probability_3h >= 0) {
      M5.Lcd.setTextFont(1);
      M5.Lcd.setTextColor(COL_ACCENT_CYAN, COL_BG);
      M5.Lcd.setTextPadding(126);
      snprintf(buf, sizeof(buf), "Rain 3h: %d%%",
               weatherData.precipitation_probability_3h);
      M5.Lcd.drawString(buf, 26, 104);
    }
  }

  M5.Lcd.setTextPadding(0);
}

void drawInfoPanel() {
  char buf[32];
  struct tm timeinfo;
  bool hasTime = ntpSynced && getLocalTime(&timeinfo, 100);

  // --- Header area: WiFi icon + time (small) ---
  M5.Lcd.fillRect(258, 1, 58, 15, COL_HEADER_BG);

  // WiFi icon in header bar
  uint16_t wifiColor = (WiFi.status() == WL_CONNECTED)
                            ? COL_ACCENT_GREEN : COL_ACCENT_RED;
  int wcx = 268, wcy = 13;
  M5.Lcd.drawCircleHelper(wcx, wcy, 10, 0x1 | 0x2, wifiColor);
  M5.Lcd.drawCircleHelper(wcx, wcy, 7, 0x1 | 0x2, wifiColor);
  M5.Lcd.drawCircleHelper(wcx, wcy, 4, 0x1 | 0x2, wifiColor);
  M5.Lcd.fillCircle(wcx, wcy, 1, wifiColor);

  // Time (small, right edge of header)
  M5.Lcd.setTextFont(1);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextDatum(TR_DATUM);
  if (hasTime) {
    M5.Lcd.setTextColor(COL_TEXT_SECONDARY, COL_HEADER_BG);
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  } else {
    M5.Lcd.setTextColor(COL_TEXT_MUTED, COL_HEADER_BG);
    snprintf(buf, sizeof(buf), "--:--");
  }
  M5.Lcd.drawString(buf, 314, 4);
  M5.Lcd.setTextDatum(TL_DATUM);

  // --- Content area ---

  // Strava total distance (hero)
  drawBicycle(164, 24, COL_ACCENT_AMBER);
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextPadding(110);
  if (stravaDataValid) {
    M5.Lcd.setTextColor(COL_ACCENT_AMBER, COL_BG);
    snprintf(buf, sizeof(buf), "%.0f km", stravaStats.all_ride_totals_km);
  } else {
    M5.Lcd.setTextColor(COL_TEXT_MUTED, COL_BG);
    snprintf(buf, sizeof(buf), "--- km");
  }
  M5.Lcd.drawString(buf, 186, 22);

  // Weekly distance label
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COL_ACCENT_GREEN, COL_BG);
  M5.Lcd.setTextPadding(155);
  snprintf(buf, sizeof(buf), "Wk  %.0fkm", weeklyDistanceKm);
  M5.Lcd.drawString(buf, 164, 50);

  // Weekly progress bar (average-based)
  {
    int barX = 164, barY = 66, barW = 150, barH = 14;
    float maxKm = weeklyAverageKm * 1.5f;
    if (maxKm < 1.0f) maxKm = 1.0f;

    // Average marker position
    int avgPos = static_cast<int>(barW * (weeklyAverageKm / maxKm));

    // Current distance position (capped to bar width)
    float ratio = weeklyDistanceKm / maxKm;
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    int fillW = static_cast<int>(barW * ratio);

    // Draw background
    M5.Lcd.fillRect(barX, barY, barW, barH, COL_HEADER_BG);

    if (weeklyDistanceKm <= weeklyAverageKm) {
      // Below average: green fill only
      if (fillW > 0) {
        M5.Lcd.fillRect(barX, barY, fillW, barH, COL_ACCENT_GREEN);
      }
    } else {
      // Above average: green up to avg, cyan for excess
      M5.Lcd.fillRect(barX, barY, avgPos, barH, COL_ACCENT_GREEN);
      M5.Lcd.fillRect(barX + avgPos, barY, fillW - avgPos, barH,
                       COL_ACCENT_CYAN);
    }

    // Average marker line
    M5.Lcd.fillRect(barX + avgPos, barY - 1, 2, barH + 2,
                     COL_TEXT_PRIMARY);
  }

  // Monthly distance + elevation
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COL_TEXT_SECONDARY, COL_BG);
  M5.Lcd.setTextPadding(155);
  snprintf(buf, sizeof(buf), "Mo %.0fkm UP %.0fm",
           monthlyDistanceKm, monthlyElevationM);
  M5.Lcd.drawString(buf, 164, 80);

  // Uptime (right-aligned, bottom)
  unsigned long uptimeSec = millis() / 1000;
  M5.Lcd.setTextFont(1);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COL_TEXT_MUTED, COL_BG);
  M5.Lcd.setTextDatum(TR_DATUM);
  M5.Lcd.setTextPadding(80);
  snprintf(buf, sizeof(buf), "Up: %lum%lus", uptimeSec / 60, uptimeSec % 60);
  M5.Lcd.drawString(buf, 316, 100);
  M5.Lcd.setTextDatum(TL_DATUM);

  M5.Lcd.setTextPadding(0);
}

static uint16_t severityColor(Severity s) {
  switch (s) {
    case Severity::NORMAL:   return COL_TEXT_PRIMARY;
    case Severity::WARNING:  return COL_WARN_YELLOW;
    case Severity::CRITICAL: return COL_CRIT_RED;
    default:                 return COL_TEXT_PRIMARY;
  }
}

void drawMaintenancePanel() {
  uint64_t now = currentCumulativeMs();
  time_t currentEpoch = 0;
  if (ntpSynced) {
    time(&currentEpoch);
  }

  // --- Tire Pressure (left half) ---
  uint64_t tireElapsedMs = 0;
  if (now > tirePressure.resetUptimeMs()) {
    tireElapsedMs = now - tirePressure.resetUptimeMs();
  }
  MaintenanceDisplayResult tireResult =
      MaintenanceDisplay::format(tirePressure.resetEpoch(), currentEpoch,
                                 tireElapsedMs);
  uint16_t tireColor = severityColor(tireResult.severity);

  // Tire icon (36x36, centered in left half)
  drawTireIcon(62, 146, tireColor);

  // Tire value
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(tireColor, COL_BG);
  M5.Lcd.setTextDatum(TC_DATUM);
  M5.Lcd.setTextPadding(150);
  M5.Lcd.drawString(tireResult.text, 80, 186);

  // --- Chain Lube (right half) ---
  MaintenanceDisplayResult chainResult =
      MaintenanceDisplay::formatDistance(chainLubeDistanceKm);
  Severity chainSeverity = RainRideDetector::applySeverityOverride(
      chainResult.severity, rainRideFlag);
  uint16_t chainColor = severityColor(chainSeverity);

  // Chain icon (72x36, centered in right half)
  drawChainIcon(204, 146, chainColor);

  // Chain value
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextColor(chainColor, COL_BG);
  M5.Lcd.setTextPadding(150);
  M5.Lcd.drawString(chainResult.text, 240, 186);

  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextPadding(0);
}

void saveToNvs() {
  unsigned long now_ms = millis();
  // Update cumulative uptime base
  cumulativeUptimeMs += (now_ms - lastUptimeUpdateMs);
  lastUptimeUpdateMs = now_ms;

  preferences.putULong64("cum_uptime", cumulativeUptimeMs);
  preferences.putULong64("tire_reset", tirePressure.resetUptimeMs());
  preferences.putULong64("chain_reset", chainLube.resetUptimeMs());
  preferences.putULong64("tire_epoch",
                          static_cast<uint64_t>(tirePressure.resetEpoch()));
  preferences.putULong64("chain_epoch",
                          static_cast<uint64_t>(chainLube.resetEpoch()));
  preferences.putFloat("chain_dist", chainLubeDistanceKm);
  preferences.putBool("rain_ride", rainRideFlag);
}

void setup() {
  M5.begin(true, true, true, false);
  M5.Power.begin();

  Serial.printf("RideReady! [%s]\n", GIT_HASH);

  M5.Lcd.fillScreen(COL_BG);
  M5.Lcd.setTextFont(4);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(COL_TEXT_PRIMARY, COL_BG);
  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.drawString("RideReady!", 160, 110);
  M5.Lcd.setTextFont(1);
  M5.Lcd.setTextColor(COL_TEXT_MUTED, COL_BG);
  M5.Lcd.drawString(GIT_HASH, 160, 140);
  M5.Lcd.setTextDatum(TL_DATUM);

  // NVS: restore cumulative uptime and maintenance reset times
  preferences.begin("rideready", false);
  cumulativeUptimeMs = preferences.getULong64("cum_uptime", 0);
  lastUptimeUpdateMs = millis();

  uint64_t tireResetMs = preferences.getULong64("tire_reset", 0);
  uint64_t chainResetMs = preferences.getULong64("chain_reset", 0);
  tirePressure.reset(tireResetMs);
  chainLube.reset(chainResetMs);

  // Restore epoch values
  uint64_t tireEpoch = preferences.getULong64("tire_epoch", 0);
  uint64_t chainEpoch = preferences.getULong64("chain_epoch", 0);
  if (tireEpoch > 0) {
    tirePressure.setResetEpoch(static_cast<time_t>(tireEpoch));
  }
  if (chainEpoch > 0) {
    chainLube.setResetEpoch(static_cast<time_t>(chainEpoch));
  }

  // Restore chain lube distance cache
  chainLubeDistanceKm = preferences.getFloat("chain_dist", 0.0f);
  if (chainLubeDistanceKm > 0.0f) {
    chainLubeDistanceValid = true;
  }

  // Restore rain ride flag
  rainRideFlag = preferences.getBool("rain_ride", false);

  // Restore weekly/monthly stats cache
  weeklyDistanceKm = preferences.getFloat("weekly_dist", 0.0f);
  weeklyAverageKm = preferences.getFloat("weekly_avg", 0.0f);
  monthlyDistanceKm = preferences.getFloat("month_dist", 0.0f);
  monthlyElevationM = preferences.getFloat("month_elev", 0.0f);

  // Restore Strava tokens
  loadStravaTokens();

  Serial.printf(
      "NVS restored: cum_uptime=%llu tire_reset=%llu chain_reset=%llu "
      "tire_epoch=%llu chain_epoch=%llu\n",
      cumulativeUptimeMs, tireResetMs, chainResetMs, tireEpoch, chainEpoch);

  bool sht_ok = sht3x.begin(&Wire, 0x44, ENV_SDA, ENV_SCL, 400000U);
  bool qmp_ok = qmp6988.begin(&Wire, 0x70, ENV_SDA, ENV_SCL, 400000U);
  sensorOk = sht_ok && qmp_ok;

  Serial.printf("SHT3X: %s, QMP6988: %s\n", sht_ok ? "OK" : "FAIL",
                qmp_ok ? "OK" : "FAIL");

  if (!sensorOk) {
    Serial.println("ENV sensor init failed");
  }

  // Read sensors immediately on boot
  if (sensorOk) {
    readSensors();
  }

  // Wi-Fi connection
  initWifi();

  // NTP time sync
  if (WiFi.status() == WL_CONNECTED) {
    initNtp();
    // Wait briefly for initial NTP sync
    delay(2000);
    ntpSynced = checkNtpSync();
    if (ntpSynced) {
      Serial.println("NTP synced");
    }
    lastNtpSyncMs = millis();

    // Initial Strava sync
    syncStrava();
    lastStravaSyncMs = millis();
    stravaSyncNeeded = false;

    // Initial weather fetch
    fetchWeather();
    lastWeatherSyncMs = millis();
    weatherSyncNeeded = false;
  }

  drawStaticLayout();
  drawEnvPanel();
  drawInfoPanel();
  drawMaintenancePanel();
}

void loop() {
  M5.update();

  unsigned long now = millis();

  // A button: disabled — GPIO39 ghost triggers (ESP32 errata) cause
  // unintended API calls that exhaust Strava rate limits.

  // B button: reset Tire Pressure timer
  if (M5.BtnB.wasReleased()) {
    uint64_t cumNow = currentCumulativeMs();
    tirePressure.reset(cumNow);
    updateResetEpoch(tirePressure);
    saveToNvs();
    drawMaintenancePanel();
    Serial.println("Tire Pressure timer reset");
  }

  // C button: reset Chain Lube
  else if (M5.BtnC.wasReleased()) {
    uint64_t cumNow = currentCumulativeMs();
    chainLube.reset(cumNow);
    updateResetEpoch(chainLube);
    chainLubeDistanceKm = 0.0f;
    chainLubeDistanceValid = false;
    rainRideFlag = false;
    preferences.putFloat("chain_dist", 0.0f);
    preferences.putBool("rain_ride", false);
    saveToNvs();
    stravaSyncNeeded = true;
    drawMaintenancePanel();
    Serial.println("Chain Lube reset (distance + rain flag cleared)");
  }

  // Wi-Fi reconnection (every 30s)
  if (WiFi.status() != WL_CONNECTED &&
      (now - lastWifiCheckMs) >= WIFI_RECONNECT_INTERVAL_MS) {
    lastWifiCheckMs = now;
    stravaSyncNeeded = true;  // Re-sync Strava after reconnect
    weatherSyncNeeded = true;  // Re-sync weather after reconnect
    Serial.println("WiFi disconnected, attempting reconnect...");
    WiFi.reconnect();
  }

  // NTP resync (every 1 hour)
  if (WiFi.status() == WL_CONNECTED &&
      (now - lastNtpSyncMs) >= NTP_RESYNC_INTERVAL_MS) {
    lastNtpSyncMs = now;
    bool wasSynced = ntpSynced;
    ntpSynced = checkNtpSync();
    if (ntpSynced && !wasSynced) {
      Serial.println("NTP synced (recovered)");
    }
  }

  // Check for first NTP sync after WiFi connects (throttled to every 5s)
  if (!ntpSynced && WiFi.status() == WL_CONNECTED &&
      (now - lastNtpCheckMs) >= NTP_CHECK_INTERVAL_MS) {
    lastNtpCheckMs = now;
    ntpSynced = checkNtpSync();
    if (ntpSynced) {
      Serial.println("NTP synced");
      lastNtpSyncMs = now;
      drawInfoPanel();
    }
  }

  // Strava sync (periodic or after reconnect)
  bool periodicStravaSync = (now - lastStravaSyncMs) >= STRAVA_SYNC_INTERVAL_MS;
  if ((stravaSyncNeeded || periodicStravaSync) && WiFi.status() == WL_CONNECTED && ntpSynced) {
    stravaSyncNeeded = false;
    lastStravaSyncMs = now;
    syncStrava();
    drawInfoPanel();
    M5.update();  // Flush stale button state after long network ops
  }

  // Weather sync (periodic or after reconnect)
  bool periodicWeatherSync = (now - lastWeatherSyncMs) >= WEATHER_SYNC_INTERVAL_MS;
  if ((weatherSyncNeeded || periodicWeatherSync) && WiFi.status() == WL_CONNECTED) {
    weatherSyncNeeded = false;
    lastWeatherSyncMs = now;
    fetchWeather();
    drawEnvPanel();
    M5.update();  // Flush stale button state after long network ops
  }

  // Periodic sensor read
  if (now - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = now;

    if (sensorOk) {
      readSensors();
      Serial.printf(
          "Temp: %.1f C  Humidity: %.1f %%  Pressure: %.1f hPa  Trend: %s\n",
          temperature, humidity, pressure_hpa,
          trendSymbol(pressureTrend.direction()));
      drawEnvPanel();
    }

    drawInfoPanel();
    drawMaintenancePanel();
  }

  // Periodic NVS save (every 60s)
  if (now - lastNvsSaveMs >= NVS_SAVE_INTERVAL_MS) {
    lastNvsSaveMs = now;
    saveToNvs();
  }
}
